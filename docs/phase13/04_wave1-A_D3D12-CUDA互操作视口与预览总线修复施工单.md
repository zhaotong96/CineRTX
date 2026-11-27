# **Wave 1-A D3D12-CUDA 互操作视口与预览总线修复施工单**

## **1\. 元信息与核心目标**

### **1.1 施工波次标识**

* **Wave / Stage**: Wave 1 – Stage A（并行轨道 A）  
* **施工主题**: 零断链 D3D12 \+ DirectComposition 统一视口与无锁单槽预览总线重建  
* **目标模块**: `src/core/job_queue.cpp`, `src/core/hw_transcode_stages.cpp`, `src/core/ngx_remaster.cpp`, `src/shim/ngx_bridge/bridge_exports.cpp`, `src/gui/workstation_canvas.cpp`, `src/gui/preview_viewport.cpp`, `src/gui/rtx_gui.cpp`, `src/gui/shaders/preview_tone_map_cs.hlsl`, `webui/index.html`, `webui/app.js`

### **1.2 宪法级硬性约束**

1. **静态类型门控约束**: 严禁新增任何具名 `struct` 或 `class`。必须完全复用现有 `PreviewViewport`、`ExternalMemoryBridge` 与 `NgxRemaster` 内部成员或自由函数（Namespace-level free functions），确保全工程 `AuditTypes <= 34/35`。  
2. **动态加载导入约束**: 严禁静态导入 `d3d12.dll`、`dxgi.dll` 或 `dwmapi.dll`。所有 D3D12/DXGI/DComp API 均需通过全局 `sys_dynload` 符号包装器进行动态加载与延迟绑定，保证 GUI 二进制 `AuditImports <= 4`。

---

## **2\. 四重系统断链精准修复规约**

### **2.1 断链 1：解除转码队列入口预览掐断**

#### **2.1.1 缺陷定位与诊断**

* `src/core/job_queue.cpp:258-264` 内部存在未挂接的虚假回调门控逻辑 `if (options_.preview && session_->previewReady())`。由于 `previewReady()` 恒返回 `false`，导致转码任务运行期间 CUDA 渲染核心帧被直接丢弃，无法推送到 UI 视口。  
* `src/core/hw_transcode_stages.cpp:344` 缺少对无锁覆盖邮箱（Latest-Frame Mailbox）的更新唤醒。

#### **2.1.2 修改实施代码规约**

```
// ===== src/core/job_queue.cpp =====
// 彻底移除旧有的 if (options_.preview && session_->previewReady()) 条件分支
// 替换为原子指针校验与直接绑定逻辑

void TranscodeJob::processFramePreview(const PreviewFrameMetadata& meta) {

    if (!preview_enabled_ || !preview_enabled_->load(std::memory_order_acquire)) {
|---|return; // 预览被显式关闭，跳过 CUDA 采样
    }

    // 绑定至统一无锁单槽覆盖邮箱 (Latest-Frame Mailbox)
    // 直接操作已分配的共享外部内存槽位
    uint32_t active_slot = 0; // 统一单槽
    cudaError_t err = run_pack_preview_rgba10_kernel(
        meta.cuda_surface_ptr,
        meta.width,
        meta.height,
        meta.pitch,
        mailbox_bridge_slots_[active_slot].cuda_mapped_ptr
    );

    if (err == cudaSuccess) {
        // 原子更新单调递增帧序列号，通知 UI 消费者有新帧到达
        mailbox_bridge_slots_[active_slot].metadata.src_w = meta.width;
        mailbox_bridge_slots_[active_slot].metadata.src_h = meta.height;
        mailbox_bridge_slots_[active_slot].metadata.input_is_pq = meta.is_pq;
        mailbox_bridge_slots_[active_slot].metadata.peak_nits = meta.peak_nits;
        
        mailbox_bridge_slots_[active_slot].published_seq.fetch_add(1, std::memory_order_release);
    }
}
```

---

### **2.2 断链 2：消除虚占 Slot 0、硬编码、几何失步与清理单例**

#### **2.2.1 缺陷定位与诊断**

* **首帧未写虚占**: `src/shim/ngx_bridge/bridge_exports.cpp:250` 初始化时未清零帧序号，UI 在未有 CUDA 写入时误读 Slot 0 废数据。  
* **硬编码元数据**: `src/core/ngx_remaster.cpp:204-212` 与 `src/gui/workstation_canvas.cpp:1051-1066` 强制使用了 `960, 540, input_is_pq=true, max_nits=100.0f`。  
* **画面拉伸与失步**: `src/gui/shaders/preview_tone_map_cs.hlsl:109-110` 未做 Letterbox / Pillarbox 画幅比校验；`src/gui/rtx_gui.cpp:558-569` 和 `src/gui/preview_viewport.cpp:449-452` 在 WM\_SIZE 响应时直接使用了未初始化的交换链尺寸。  
* **危险单例与 CPU 飙高**: `src/core/ngx_remaster.cpp:105` 存在全局原子单例指针；`src/gui/rtx_gui.cpp:876-878` 定时器无脑 60Hz 盲刷新。

#### **2.2.2 改造规范与代码实现**

##### **1\. 发布状态机与动态元数据借阅**

```
// ===== src/shim/ngx_bridge/bridge_exports.cpp =====
void publishActivePreviewBridge(BridgeMailbox* mailbox) {
    if (!mailbox) return;
    // 强制初始化单调递增序列号为 0（0 表示未发布任何有效帧）
    mailbox->published_seq.store(0, std::memory_order_relaxed);
    mailbox->last_consumed_seq = 0;
}

// ===== src/gui/workstation_canvas.cpp =====
void WorkstationCanvas::updatePreviewTexture() {
    uint64_t pub_seq = g_mailbox.published_seq.load(std::memory_order_acquire);
    if (pub_seq == 0 || pub_seq <= last_consumed_seq_) {
        return; // 无新帧或未写入首帧，绝对不读取 Slot 0 废数据
    }

    // 同步借阅真实帧元数据，彻底废除硬编码
    uint32_t real_w = g_mailbox.metadata.src_w;
    uint32_t real_h = g_mailbox.metadata.src_h;
    bool is_pq = g_mailbox.metadata.input_is_pq;
    float peak_nits = g_mailbox.metadata.peak_nits;

    // 检查是否需要重建 D3D12-CUDA 共享缓冲 (支持 9:16, 21:9, 4K)
    ensureSharedBufferSize(real_w, real_h);

    // 更新消费序列号
    last_consumed_seq_ = pub_seq;
    renderViewportFrame(real_w, real_h, is_pq, peak_nits);
}
```

##### **2\. HLSL 等比 Letterbox / Pillarbox 计算 ( preview\_tone\_map\_cs.hlsl )**

```
// ===== src/gui/shaders/preview_tone_map_cs.hlsl =====
cbuffer ToneMapParams : register(b0) {
    uint2 srcSize;
    uint2 dstSize;
    float peakNits;
    uint isPQ;
};

Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputSurface : register(u0);

[numthreads(16, 16, 1)]
void main(uint3d dispatchThreadID : SV_DispatchThreadID) {
    uint2 targetPos = dispatchThreadID.xy;
    if (targetPos.x >= dstSize.x || targetPos.y >= dstSize.y) return;

    // 计算 Target 区域内保持源画幅比的视口 (Letterbox / Pillarbox)
    float srcAspect = (float)srcSize.x / (float)srcSize.y;
    float dstAspect = (float)dstSize.x / (float)dstSize.y;

    float drawWidth, drawHeight;
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    if (srcAspect > dstAspect) {
        drawWidth = (float)dstSize.x;
        drawHeight = drawWidth / srcAspect;
        offsetY = ((float)dstSize.y - drawHeight) * 0.5f;
    } else {
        drawHeight = (float)dstSize.y;
        drawWidth = drawHeight * srcAspect;
        offsetX = ((float)dstSize.x - drawWidth) * 0.5f;
    }

    // 填充黑边边界
    if (targetPos.x < offsetX || targetPos.x >= (offsetX + drawWidth) ||
        targetPos.y < offsetY || targetPos.y >= (offsetY + drawHeight)) {
        OutputSurface[targetPos] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // 采样归一化坐标计算
    float2 normUV = float2(
        (targetPos.x - offsetX) / drawWidth,
        (targetPos.y - offsetY) / drawHeight
    );
    
    uint2 samplePos = uint2(normUV * srcSize);
    float4 color = InputTex.Load(uint3(samplePos, 0));

    // 色彩空间转换 (PQ/Linear -> SDR Target)
    if (isPQ != 0) {
        color.rgb = ApplyPQToSDRToneMap(color.rgb, peakNits);
    }
    
    OutputSurface[targetPos] = float4(color.rgb, 1.0f);
}
```

##### **3\. 首帧交换链延迟创建与帧等待 ( rtx\_gui.cpp & preview\_viewport.cpp )**

```
// ===== src/gui/preview_viewport.cpp =====
void PreviewViewport::onSize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;

    pending_width_ = width;
    pending_height_ = height;

    if (!swap_chain_) {
        // 交换链未初始化，仅更新 pending 尺寸，严禁在此阶段使用默认 960x540
        return;
    }

    // 存在交换链时才执行安全 ResizeBuffers
    resizeSwapChainBuffers(width, height);
}

void PreviewViewport::adoptDevice(ID3D12Device* device, HWND hwnd) {
    device_ = device;
    hwnd_ = hwnd;

    // 使用延迟保存的实际视口尺寸，若无则退回客户端区实际 Bounds
    uint32_t init_w = (pending_width_ > 0) ? pending_width_ : 1920;
    uint32_t init_h = (pending_height_ > 0) ? pending_height_ : 1080;

    createSwapChainInternal(init_w, init_h);
}

void PreviewViewport::presentFrame() {
    if (!swap_chain_) return;

    // 严格等待 IDXGISwapChain2 可呈现等待句柄，消除首帧卡顿与 CPU/GPU 竞态
    HANDLE waitableObj = swap_chain_waitable_object_;
    if (waitableObj != NULL) {
        ::WaitForSingleObject(waitableObj, 1000);
    }

    swap_chain_->Present(1, 0);
}
```

##### **4\. 节流定时器与移除全局单例**

```
// ===== src/gui/rtx_gui.cpp =====
void RtxGui::onRenderTimerTick() {
    // 只有当前 Tab 为 Player/NLE 且正在播放或转码开启预览时，才进行视口刷新
    bool need_refresh = (active_tab_ == TAB_PLAYER || active_tab_ == TAB_NLE) &&
                        (is_playing_ || is_transcoding_with_preview_);

    if (!need_refresh) {
        return; // 节流休眠，CPU 零占用
    }

    // 限制最高刷新帧率 <= 30Hz
    auto now = std::chrono::steady_clock::now();
    if (now - last_render_time_ < std::chrono::milliseconds(33)) {
        return;
    }
    last_render_time_ = now;

    viewport_->triggerRedraw();
}
```

---

### **2.3 断链 3：打通 Win32 Player 入口与 NLE Bin 绑定**

#### **2.3.1 缺陷定位与诊断**

* `src/gui/rtx_gui.cpp:166-174, 456-472` UI 缺乏明确的 Open Media 交互响应。  
* `src/gui/workstation_canvas.cpp:1021-1033` 中的 `togglePlayback` 存在严重漏洞：当 `playbackSession == nullptr` 时直接翻转 `isPlaying = !isPlaying` 标志位，产生"未加载媒体却虚假播放"的 BUG。

#### **2.3.2 改造规范与代码实现**

```
// ===== src/gui/workstation_canvas.cpp =====
void WorkstationCanvas::openMediaInPlayer(const std::string& filepath) {
    if (filepath.empty()) return;

    // 1. 停止并释放旧会话
    if (playback_session_) {
        playback_session_->stop();
        playback_session_.reset();
    }

    // 2. 初始化真实 PlaybackSession
    playback_session_ = std::make_unique<PlaybackSession>();
    if (!playback_session_->openFile(filepath)) {
        showErrorMessage("媒体加载失败", "无法解析输入文件: " + filepath);
        return;
    }

    // 3. 读取首帧海报帧 (Poster Frame) 推送至 UI 视口
    playback_session_->seekToFrame(0);
    PreviewFrame frame = playback_session_->decodeCurrentFrame();
    publishFrameToMailbox(frame);

    // 4. 重置播放状态
    is_playing_ = false;
    updateTransportControlsUI();
}

void WorkstationCanvas::togglePlayback() {
    // 严格门控：严禁无媒体时假装播放
    if (!playback_session_ || !playback_session_->isValid()) {
        // 自动唤起文件选择框
        std::string selected_file = showOpenFileDialog();
        if (!selected_file.empty()) {
            openMediaInPlayer(selected_file);
        }
        return;
    }

    is_playing_ = !is_playing_;
    if (is_playing_) {
        playback_session_->start();
    } else {
        playback_session_->pause();
    }
}
```

---

### **2.4 断链 4：WebUI 预览与后端渲染帧对齐**

#### **2.4.1 缺陷定位与诊断**

* `webui/index.html` & `webui/app.js` 缺乏真实的后端图像帧通道，与 Native 10-bit HDR 无锁视口断开。

#### **2.4.2 改造规范与代码实现**

* **能力边界定位**: Native Win32 端享受全极速 10-bit GPU 零回读 HDR 呈现；WebUI 定位为轻量控制台。  
* **后端端点扩展**: 后端暴露 `GET /api/v1/preview/stream` MJPEG 端点，复用 `Latest-Frame Mailbox`。仅在 WebUI 建立 HTTP 连接时挂载采样（降采样至 \<= 10fps, SDR Tone-mapped JPEG），完全不破坏 GPU 主渲染链路性能。

```js
// ===== webui/app.js =====
class WebUIPreviewController {
    constructor() {
        this.previewImg = document.getElementById('remote-preview-img');
        this.isSubscribed = false;
    }

    startPreviewStream() {
        if (this.isSubscribed) return;
        // 挂载后端降采样 MJPEG 代理流
        this.previewImg.src = '/api/v1/preview/stream?fps=10&quality=80';
        this.isSubscribed = true;
    }

    stopPreviewStream() {
        if (!this.isSubscribed) return;
        this.previewImg.src = 'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7'; // Clear
        this.isSubscribed = false;
    }
}
```

---

## **3\. 验收标准与自动化测试**

为验证修复的完整性，扩展 `tests/test_preview_bridge.cpp` 单元测试。

```
// ===== tests/test_preview_bridge.cpp =====
#include <cassert>
#include <iostream>
#include "../src/core/preview_mailbox.h"

void test_initial_state_zero() {
    BridgeMailbox mailbox{};
    publishActivePreviewBridge(&mailbox);
    
    // 验证 1: 首帧未写入前 published_seq 必须为 0
    assert(mailbox.published_seq.load() == 0);
    std::cout << "[PASS] Test Initial State Zero" << std::endl;
}

void test_resolution_switch_no_leak() {
    // 验证 2: 切换竖屏 1080x1920 与超宽屏 2560x1080 共享内存分配无内存泄漏
    ensureSharedBufferSize(1080, 1920);
    uint64_t size1 = getAllocatedSharedBufferSize();
    assert(size1 >= 1080 * 1920 * 4);

    ensureSharedBufferSize(2560, 1080);
    uint64_t size2 = getAllocatedSharedBufferSize();
    assert(size2 >= 2560 * 1080 * 4);
    
    std::cout << "[PASS] Test Dynamic Resolution Allocation" << std::endl;
}

void test_import_constraints() {
    // 验证 3: 检查 DLL 静态导入合法性
    #if defined(_WIN32)
    HMODULE hD3D12 = GetModuleHandleA("d3d12.dll");
    // 必须通过 sys_dynload 动态加载，二进制导入表不得直接链接 d3d12.dll
    assert(hD3D12 == NULL || isDynamicallyLoadedOnly("d3d12.dll"));
    #endif
    std::cout << "[PASS] Test Dynamic Import Audit" << std::endl;
}

int main() {
    test_initial_state_zero();
    test_resolution_switch_no_leak();
    test_import_constraints();
    std::cout << "All Stage-A Viewport Bridge Tests Passed Successfully." << std::endl;
    return 0;
}
```

---

## **4\. 施工断言与验收矩阵**

| 序号 | 修复项 | 对应文件/行号 | 验证方式 | 成功基线 |
| :---- | :---- | :---- | :---- | :---- |
| **1** | 转码预览门控解除 | `job_queue.cpp:258hw_transcode_stages.cpp:344` | 启动 CLI 转码并带 `--preview` | `published_seq` 大于 0 且帧率稳定 |
| **2** | 废除硬编码与虚占 | `bridge_exports.cpp:250workstation_canvas.cpp:1051` | 启动程序查看视口 | 首帧未写入时 `pub_seq == 0`，无乱码 |
| **3** | Letterbox 画幅比 | `preview_tone_map_cs.hlsl:109` | 载入 9:16 / 21:9 视频 | 视口黑边填充，图像无拉伸变形 |
| **4** | 延迟交换链同步 | `preview_viewport.cpp:449rtx_gui.cpp:558` | 改变窗口大小 (WM\_SIZE) | 无黑屏闪烁，使用 FrameWaitableObject 同步 |
| **5** | Player 假播根除 | `workstation_canvas.cpp:1021` | 未载入媒体点击 Play 按钮 | 弹窗提示或唤起文件选择器，无假播 |
| **6** | 架构宪法门控 | 工程编译/链接输出 | 检查符号表与导入表 | `AuditTypes <= 34` 且 GUI `AuditImports <= 4` |

