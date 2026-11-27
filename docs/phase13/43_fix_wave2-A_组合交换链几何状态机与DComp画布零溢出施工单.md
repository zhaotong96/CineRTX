# **43\_fix\_wave2-A\_组合交换链几何状态机与DComp画布零溢出施工单**

## **1\. 施工元数据与基线硬红线**

### **1.1 施工元数据**

* **施工波次**：Fix Wave 2 \- Stage A  
* **执行依赖**：必须在 Fix Wave 1-A / Fix Wave 1-B 完全闭环后启动。  
* **治理目标**：彻底根治问题 2B（Encoder 转码预览尺寸未自适应中间画布、启动首帧 960x540 锁定、工作区切换或窗口缩放后物理像素直接冲破画布边界遮挡左右面板）。

### **1.2 基线与类型限制（基线锁定：Commit `d18b469`）**

```
+-----------------------------------------------------------------------------------+

|                                 基线硬红线卡尺                                    |
|---|
| AuditTypes Count                  | 34 (HEADLESS) / 35 (GUI)                      |
| AuditImports Count                | <= 4 (GUI) / == 2 (HEADLESS)                  |
| Private State Storage             | targetWidth_, targetHeight_, swapWidth_,      |
|                                   | swapHeight_ (私有 POD 标量，严禁新增具名类)   |
| Dynamic COM Interface             | IDCompositionRectangleClip 必须经由既有       |
|                                   | sys_dynload 对象的 QueryInterface 获取        |
+-----------------------------------+-----------------------------------------------+
```

### **1.3 组合交换链 (Composition SwapChain) 硬性 API 红线**

1. **禁止自动探测**：`CreateSwapChainForComposition` 创建的交换链，其后续 `ResizeBuffers` 调用**严禁传入 `(0, 0)`** 依赖 DXGI 自动探测宿主窗口矩形。必须传入经过 `GetClientRect` 明确计算出的物理像素值 `(targetWidth_, targetHeight_)`。  
2. **D3D12 资源释放序律**：在 D3D12 下执行 `ResizeBuffers` 之前，必须调用 `waitForGpuFence()` 阻塞等待所有飞行帧（Flight Frames）执行完毕，且必须**彻底释放全部保存的后备缓冲（Back Buffer）`ComPtr` 引用与 RTV/UAV 描述符**，严禁悬挂任何显存资源引用。

---

## **2\. 四重几何失步根因坐标**

```
+-----------------------------------------------------------------------------------+
|                             四重几何失步根因诊断                                  |
+------+---------------------------------+------------------------------------------+
| 坐标 | 代码位置                        | 根因机制分析                             |
+------+---------------------------------+------------------------------------------+
| 1    | rtx_gui.cpp: 615–626            | WM_CREATE 按 960x540 初始化 PreviewView- |
|      |                                 | port，随后 applyWorkspaceLayout 重新计算 |
|      |                                 | 画布，但此时 swap_chain_ 未建立，导致计算 |
|      |                                 | 出来的真实尺寸被丢弃。                   |
+------+---------------------------------+------------------------------------------+
| 2    | rtx_gui.cpp: 615–626            | WM_CREATE 流程中漏掉对                   |
|      |                                 | commitViewportVsrScale(hwnd) 的同步调用。 |
+------+---------------------------------+------------------------------------------+
| 3    | PreviewViewport / DirectComposition| CreateSwapChainForComposition 不会自动随  |
|      |                                 | 宿主窗口缩放，且缺乏 DComp 矩形硬裁剪     |
|      |                                 | (SetClip)，致 960x540 物理像素冲破画布。  |
+------+---------------------------------+------------------------------------------+
| 4    | preview_tone_map_cs.hlsl        | 渲染管线未按源宽高与交换链物理宽高计算    |
|      | presentShared()                 | 等比居中 drawRect，导致非 16:9 媒体拉伸   |
|      |                                 | 或画面越界。                             |
+------+---------------------------------+------------------------------------------+
```

---

## **3\. Task 1：补齐 `rtx_gui.cpp` `WM_CREATE` 与工作区切换的布局/VSR 缩放同步链**

### **Step 1.1 `WM_CREATE` 同步链补齐**

在 `src/gui/rtx_gui.cpp` 的 `WM_CREATE` 消息处理逻辑（第 615–626 行）中，重新声明窗口创建样式并补齐 VSR 缩放同步点：

* 创建主窗口（Host Window）时必须确保样式中包含 `WS_CLIPCHILDREN`；  
* 创建预览子窗口 `child_hwnd_` 时，必须指定 `WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS`；  
* 在调用 `applyWorkspaceLayout(hwnd)` 完成布局计算后，**紧接补齐 `commitViewportVsrScale(hwnd)` 的显式同步调用**。

```
// src/gui/rtx_gui.cpp - WM_CREATE 修复
case WM_CREATE: {
    // 1. 设置主窗口样式包含 WS_CLIPCHILDREN
    SetWindowLongPtr(hwnd, GWL_STYLE, GetWindowLongPtr(hwnd, GWL_STYLE) | WS_CLIPCHILDREN);
    
    // 2. 创建子窗口 child_hwnd_
    child_hwnd_ = CreateWindowExW(
        0, L"PreviewViewportClass", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 960, 540, hwnd, (HMENU)101, hInstance, nullptr
    );

    // 3. 布局计算并立即同步 VSR 物理尺寸
    applyWorkspaceLayout(hwnd);
    commitViewportVsrScale(hwnd); // 补齐关键同步点
    return 0;
}
```

### **Step 1.2 `WM_SIZE` / 工作区切换 / `WM_DPICHANGED` / 全屏退出闭环**

在所有引发窗口几何变动的事件链中，统一执行确定性尺寸刷同步。

```
// 尺寸与工作区变动事件收敛处理
void handleWindowGeometryChange(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // A. 最小化保护：SIZE_MINIMIZED (0x0) 直接跳过
    if (wParam == SIZE_MINIMIZED) {
        return;
    }

    // B. DPI 换算 (pixels = dips * dpi / 96) 与布局更新
    // 由 previewViewport->setBounds(...) 唯一控制 child_hwnd_ 坐标
    applyWorkspaceLayout(hwnd);

    // C. 布局完成点显式刷入 VSR 缩放与交换链尺寸
    commitViewportVsrScale(hwnd);
}
```

* **触发点覆盖**：  
  1. `WM_SIZE` 处理函数（非 `SIZE_MINIMIZED` 情况下）；  
  2. 工作区切换（`Encoder` \$\\leftrightarrow\$ `Player` \$\\leftrightarrow\$ `NLE`）；  
  3. `WM_DPICHANGED` 响应逻辑；  
  4. 按 `Esc` 键退出无边框全屏模式的恢复点。

---

## **4\. Task 2：重构 `PreviewViewport` 为确定性状态机**

### **Step 2.1 状态分离与 `setBounds` 保护机制**

在 `PreviewViewport` 私有成员中维护期望尺寸与物理尺寸，解耦布局更新与渲染资源分配。

```
// PreviewViewport 私有成员定义 (严格保持 AuditTypes 不变)
class PreviewViewport {
private:
    // 期望物理尺寸 (窗口布局决定的尺寸)
    uint32_t targetWidth_{0};
    uint32_t targetHeight_{0};

    // 物理交换链尺寸 (当前 GPU DXGI Buffer 的实际尺寸)
    uint32_t swapWidth_{0};
    uint32_t swapHeight_{0};

    // DirectComposition 裁剪接口 (经由既有 sys_dynload QueryInterface 获取)
    Microsoft::WRL::ComPtr<IDCompositionRectangleClip> clip_;
    // ... 既有成员
public:
    void setBounds(int x, int y, int width, int height);
    void resizeSwapChain();
    void adoptDevice(ID3D12CommandQueue* queue);
};

void PreviewViewport::setBounds(int x, int y, int width, int height) {
    // 1. 移动子窗口
    SetWindowPos(child_hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

    // 2. 读取真实客户端物理像素
    RECT rect{};
    GetClientRect(child_hwnd_, &rect);
    targetWidth_ = static_cast<uint32_t>(std::max(1L, rect.right - rect.left));
    targetHeight_ = static_cast<uint32_t>(std::max(1L, rect.bottom - rect.top));

    // 3. 交换链未初始化时保存目标尺寸后安全返回 (绝不丢弃最新尺寸)
    if (!swap_chain_) {
        return;
    }

    // 4. 若交换链已存在，触发安全尺寸变更
    resizeSwapChain();
}
```

### **Step 2.2 `adoptDevice()` 首次建链与 DComp `SetClip` 锁定**

当渲染设备准备就绪，首次进入 `adoptDevice()` 时，以最新的 `targetWidth_ / targetHeight_` 进行建链并施加 DComp 硬裁剪。p  
void PreviewViewport::adoptDevice(ID3D12CommandQueue\* commandQueue) {  
// 1\. 获取最新物理像素  
RECT rect{};  
GetClientRect(child\_hwnd\_, \&rect);  
targetWidth\_ \= static\_cast\<uint32\_t\>(std::max(1L, rect.right \- rect.left));  
targetHeight\_ \= static\_cast\<uint32\_t\>(std::max(1L, rect.bottom \- rect.top));

```
// 2. 创建组合交换链 (CreateSwapChainForComposition)
DXGI_SWAP_CHAIN_DESC1 sd{};
sd.Width = targetWidth_;
sd.Height = targetHeight_;
sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
sd.Stereo = FALSE;
sd.SampleDesc.Count = 1;
sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
sd.BufferCount = 2; // BufferCount >= 2
sd.Scaling = DXGI_SCALING_STRETCH;
sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

dxgiFactory_->CreateSwapChainForComposition(commandQueue, &sd, nullptr, &swap_chain_);

swapWidth_ = targetWidth_;
swapHeight_ = targetHeight_;

// 3. DirectComposition 绑定与 SetClip 硬裁剪
dcompDevice_->CreateTargetForHwnd(child_hwnd_, FALSE, &dcompTarget_);
dcompDevice_->CreateVisual(&visual_);
visual_->SetContent(swap_chain_.Get());

// 从既有 sys_dynload COM 对象获取 IDCompositionRectangleClip
dcompDevice_->CreateRectangleClip(&clip_);
clip_->SetLeft(0.0f);
clip_->SetTop(0.0f);
cli
```

p\_-\>SetRight(static\_cast(swapWidth\_));  
clip\_-\>SetBottom(static\_cast(swapHeight\_));

```
visual_->SetClip(clip_.Get());
dcompTarget_->SetRoot(visual_.Get());
dcompDevice_->Commit();
```

}

````

### Step 2.3 安全的 D3D12 `resizeSwapChain()` 状态更新

```cpp
void PreviewViewport::resizeSwapChain() {
    // 0. 零变更或无效尺寸拦截
    if (targetWidth_ == swapWidth_ && targetHeight_ == swapHeight_) return;
    if (targetWidth_ == 0 || targetHeight_ == 0) return;

    // 1. 阻塞等待 GPU 飞行帧完成
    waitForGpuFence();

    // 2. 彻底释放所有后备缓冲引用 (D3D12 硬性红线)
    for (uint32_t i = 0; i < BufferCount; ++i) {
        backBuffers_[i].Reset();
    }
    // 释放 RTV/UAV 描述符绑定...

    // 3. 显式调整尺寸 (严禁传入 0, 0)
    HRESULT hr = swap_chain_->ResizeBuffers(
        2, 
        targetWidth_, 
        targetHeight_, 
        DXGI_FORMAT_UNKNOWN, 
        0
    );
    if (FAILED(hr)) {
        handleDeviceLost();
        return;
    }

    // 4. 更新内部记录与重建后备缓冲视图
    swapWidth_ = targetWidth_;
    swapHeight_ = targetHeight_;
    recreateBackBufferViews();

    // 5. 同步更新 DComp 裁剪矩形范围并提交
    if (clip_) {
        clip_->SetRight(static_cast<float>(swapWidth_));
        clip_->SetBottom(static_cast<float>(swapHeight_));
        dcompDevice_->Commit();
    }
}
````

---

# **5\. Task 3：打通 `presentShared()` 兜底对齐与 HLSL 等比居中 `drawRect`**

## **Step 3.1 呈现前尺寸兜底校验**

在 `PreviewViewport::presentShared()` 渲染呈现入口，增加防抖与维度兜底检查：

```
void PreviewViewport::presentShared(ID3D12GraphicsCommandList* cmdList, Resource* srcFrame) {
    // 尺寸一致性兜底拦截：确保任意异步帧到达时，物理交换链 100% 贴合当前画布
    if (targetWidth_ != swapWidth_ || targetHeight_ != swapHeight_) {
        resizeSwapChain();
    }

    // 执行后续 Draw / ToneMap 流程...
}
```

## **Step 3.2 HLSL 等比居中 (Letterbox / Pillarbox) 映射计算**

在 CPU 端根据源帧尺寸与交换链物理尺寸计算 `drawRect`，并通过根常量（Push Constants / Root Constants）传递至 `preview_tone_map_cs.hlsl`。

```
+-----------------------------------------------------------------------------------+
|                           画幅等比缩放与填充视效示意                              |
+-----------------------------------------------------------------------------------+
|  [Pillarbox 模式: 竖屏 9:16 媒体]            [Letterbox 模式: 超宽 21:9 媒体]      |
|  +-----------------------------+             +-----------------------------+      |
|  |######|               |######|             |#############################|      |
|  |######|               |######|             |                             |      |
|  |######|   Content     |######|             |           Content           |      |
|  |######|               |######|             |                             |      |
|  |######|               |######|             |#############################|      |
|  +-----------------------------+             +-----------------------------+      |
|  (黑色填充: drawRect 外部像素)               (黑色填充: drawRect 外部像素)        |
+-----------------------------------------------------------------------------------+
```

### **CPU 映射算子：**

\$\$\\text{scale} \= \\min\\left(\\frac{\\text{swapW}}{\\text{srcW}}, \\frac{\\text{swapH}}{\\text{srcH}}\\right)\$\$

\$\$\\text{drawW} \= \\max\\left(1, \\lfloor \\text{srcW} \\times \\text{scale} \+ 0.5 \\rfloor\\right), \\quad \\text{drawH} \= \\max\\left(1, \\lfloor \\text{srcH} \\times \\text{scale} \+ 0.5 \\rfloor\\right)\$\$

\$\$\\text{offX} \= \\frac{\\text{swapW} \- \\text{drawW}}{2}, \\quad \\text{offY} \= \\frac{\\text{swapH} \- \\text{drawH}}{2}\$\$

### **HLSL 着色器实现 (`preview_tone_map_cs.hlsl`)：**

```
cbuffer ViewportParams : register(b0) {
    uint srcW;
    uint srcH;
    uint swapW;
    uint swapH;
    uint drawW;
    uint drawH;
    uint offX;
    uint offY;
};

Texture2D<float4> InputFrame : register(t0);
RWTexture2D<float4> OutputCanvas : register(u0);
SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint x = dispatchThreadID.x;
    uint y = dispatchThreadID.y;

    if (x >= swapW || y >= swapH) return;

    // 判断当前像素是否位于 drawRect 区域内部
    if (x >= offX && x < (offX + drawW) && y >= offY && y < (offY + drawH)) {
        // 计算映射到源图像的 normalized UV
        float uvX = (float(x - offX) + 0.5f) / float(drawW);
        float uvY = (float(y - offY) + 0.5f) / float(drawH);
        
        // 双线性采样源帧并进行色调映射
        float4 color = InputFrame.SampleLevel(LinearSampler, float2(uvX, uvY), 0);
        OutputCanvas[uint2(x, y)] = toneMap(color);
    } else {
        // drawRect 外部像素严格填充纯黑背景色 (Letterbox / Pillarbox)
        OutputCanvas[uint2(x, y)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
}
```

---

# **6\. 验收 Gate（波次退出判据）**

施工完成后，必须逐项通过以下自动化与人工验证 Gate：

```
+-----------------------------------------------------------------------------------+
|                                 波次验收 Gate 清单                                |
+---+------------------------+------------------------------------------------------+
| # | 测试场景               | 判定标准                                             |
+---+------------------------+------------------------------------------------------+
| 1 | 启动首帧 Encoder 预览  | 启动后立即发起转码，首帧画面严格贴合中间画布矩形，    |
|   |                        | 左右无 1 像素溢出遮挡检查器或队列面板。              |
+---+------------------------+------------------------------------------------------+
| 2 | 动态窗口缩放 & 最小化  | 拖拽主窗口、最小化后还原（0x0 保护生效），图像无拉伸， |
|   |                        | 无 D3D12 设备丢失 (Device Removed) 报错。             |
+---+------------------------+------------------------------------------------------+
| 3 | 全屏切换与 DPI 响应    | Esc 退出无边框全屏、100% <-> 150% 系统 DPI 切换、     |
|   |                        | Encoder <-> Player <-> NLE 往返切换：GetClientRect    |
|   |                        | 与 GetDesc1 尺寸 100% 吻合。                         |
+---+------------------------+------------------------------------------------------+
| 4 | 架构基线卡尺静态检查   | AuditTypes 等于 34 (HEADLESS) / 35 (GUI)；           |
|   |                        | AuditImports GUI <= 4，HEADLESS == 2。                |
+---+------------------------+------------------------------------------------------+
```

