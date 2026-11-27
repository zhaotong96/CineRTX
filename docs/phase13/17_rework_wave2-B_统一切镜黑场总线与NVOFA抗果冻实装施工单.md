# **17\_rework\_wave2-B\_统一切镜黑场总线与NVOFA抗果冻实装施工单**

Superpowers 原子执行返工规约 \| 依据协调负责人驳回裁决 `CINERTX-P13-RULING-013` 第 13 号文档

---

## **1\. 元信息与驳回定性**

### **1.1 返工波次定位**

* **波次代码**：Rework Wave 2-B（与 Rework Wave 2-A 并行推进）  
* **核心使命**：从零补实原 Wave 2-B"未开工"缺口，建立全链路统一 GPU 切镜/黑场/淡入淡出检测原语，彻底根除 NVOFA（NVIDIA Optical Flow Accelerator）光流切镜果冻效应与跨镜头画面撕裂拉丝。

### **1.2 驳回定性复盘 (CINERTX-P13-RULING-013)**

裁决审查明确指出：**原 Wave 2-B（07 号文档）被定性为"未开工（NOT STARTED）"**。

* **事实查证**：`src/shim/ngx_bridge/nvof_engine.cpp`（或 `bridge_exports.cpp:674-723`）与 `src/core/hw_transcode_stages.cpp:446-459` 在上一轮交付中未包含任何边界判定逻辑；  
* **死代码悬空**：`hw_transcode_internal.h:242` 中声明的 `ShotGroupSegmenter segmenter_` 成员对象处于完全悬空未接入状态，未对接光流计算引擎与播放器主渲染管线。

### **1.3 宪法级硬红线**

1. **结构体宪法限制**：严格遵循 `AuditTypes <= 34/35` 规约，**零新增**任何具名 `struct` 或 `class`。所有数据结构复用既有 `ShotGroupSegmenter`、自由函数（Free Functions）以及既有 POD 数组/小标量缓冲区。  
2. **零 CPU 像素回读**：全链路禁止将帧数据从 GPU 内存回读至 CPU 主存（Zero Host Readback）。统计归约（SAD、Luma Mean/Var、Histogram L1）必须全程在 CUDA Stream 内异步完成。

---

## **2\. Task 1：升级统一 GPU 时域边界探测原语并盘活 ShotGroupSegmenter**

### **2.1 精确代码坐标**

* `src/shim/ngx_bridge/rife_engine.cpp:640-671`  
* `src/core/hw_transcode_internal.h:242`  
* `src/core/hw_transcode_stages.cpp:79`

### **2.2 TDD 执行步骤**

#### **Step 1.1：抽离公共 CUDA 时域探测核函数**

将原 RIFE 引擎内部的 `launchEvalBlockSad8x8` 提炼并重构为全管线共享的统一 CUDA 探测原语：

```
// 导出自 src/shim/ngx_bridge/rife_engine.cpp 并声明于公共头文件
cudaError_t launchUnifiedTemporalDetector(
    const cudaSurfaceObject_t surfPrev,
    const cudaSurfaceObject_t surfCurr,
    int width,
    int height,
    float* d_metrics_out, // Pinned/Device 小标量槽位: [sad_ema_ratio, luma_mean_prev, luma_var_prev, luma_mean_curr, luma_var_curr, hist_l1_dist]
    cudaStream_t stream
);
```

单次 Kernel 调用在 CUDA 内完成并行归约，输出三项核心标量指标至 Pinned/Device 内存槽位：

| 标量指标 | 算法逻辑与公式 | 决策阈值与物理意义 |
| :---- | :---- | :---- |
| **`sad_ema_ratio`** | \$8 \\times 8\$ 分块 SAD 聚合，计算当前帧对 SAD 相对自适应历史 EMA 滑窗的比值：\$\$\\text{Ratio} \= \\frac{\\text{SAD}*{\\text{curr}}}{\\text{EMA}*{\\text{sad}} \+ \\epsilon}\$\$ | 判定硬切镜头（`HardCut`）。当 Ratio 超过设定的突变阈值（默认 `3.8f`）时触发。 |
| **`luma_mean` & `luma_var`** | 采样计算当前帧与前一帧的 Y 通道直流均值与方差：\$\$\\mu \= \\frac{1}{N}\\sum Y, \\quad \\sigma^2 \= \\frac{1}{N}\\sum (Y \- \\mu)^2\$\$ | 判定纯黑场/极低对比度渐变（`BlackFade`）。当任一帧 \$\\sigma^2 \< \\text{fade\_var\_thresh}\$（如 `4.0f`）且 \$\\mu\$ 发生跨帧单调跳变时显式标记黑场边界。 |
| **`luma_hist_l1`** | 16-bin 归一化亮度直方图 \$H\_t, H\_{t-1}\$ 的 L1 距离：\$\$\\text{L1} \= \\sum\_{k=0}^{15} | H\_t\[k\] \- H\_{t-1}\[k\] |

#### **Step 1.2：盘活 `segmenter_` 状态机**

在 `hw_transcode_stages.cpp`（以及播放器帧循环）解码出每一帧数据后、进入任何插帧分支（NVOFA 或 RIFE）之前，统一调用 `launchUnifiedTemporalDetector`，驱动 `segmenter_` 更新状态：

```
// 位于 src/core/hw_transcode_stages.cpp:79 核心逻辑
void HardwareTranscodePipeline::ProcessFrameBoundary(const FrameData& prevFrame, const FrameData& currFrame, cudaStream_t stream) {
    // 1. 执行异步 CUDA 探测核函数
    launchUnifiedTemporalDetector(prevFrame.surface, currFrame.surface, width_, height_, d_metrics_slot_, stream);
    
    // 2. 依据 GPU 归约指标更新 segmenter_ 状态机
    BoundaryDecision decision = segmenter_.EvaluateAndAdvance(d_metrics_slot_);
    
    // 3. 彻底消除 hw_transcode_internal.h:242 悬空状态
    this->current_boundary_decision_ = decision; // Continuous | HardCut | BlackFade
}
```

---

## **3\. Task 2：NVOFA 补齐抗果冻与历史提示复位机制**

### **3.1 精确代码坐标**

* `src/core/hw_transcode_stages.cpp:446-459`  
* `src/shim/ngx_bridge/bridge_exports.cpp:674-723`  
* `src/shim/ngx_bridge/nvof_engine.cpp`

### **3.2 步骤细则**

```
                 [帧对 (I0, I1) 输入]
                          │
                          ▼
            [Unified Temporal Detector]
                          │
           ┌──────────────┴──────────────┐
           ▼                             ▼
    [HardCut / BlackFade]          [Continuous]
           │                             │
           ▼                             ▼
┌──────────────────────────┐  ┌──────────────────────────┐
│  NVOFA 硬切旁路 (Bypass)  │  │   NVOFA 正常光流估算     │
│  t < 0.5  => 输出 I0     │  │  (若前帧为切镜，置位    │
│  t >= 0.5 => 输出 I1     │  │   reset_temporal_hints)  │
└──────────┬───────────────┘  └──────────┬───────────────┘
           │                             │
           │                             ▼
           │                  [前景遮挡双向一致性校验]
           │                  [残差超限 => 退回单向采样]
           │                             │
           └──────────────┬──────────────┘
                          ▼
             [下发 VSR Reset & IDR 注入]
```

#### **Step 2.1：边界帧零果冻硬切旁路**

在 `hw_transcode_stages.cpp:446-459` 调用 NVOFA 插帧逻辑的前置判断中，检查 `segmenter_` 当前帧对决策：

```
// src/core/hw_transcode_stages.cpp:446-459 重写
if (this->current_boundary_decision_ == BoundaryDecision::HardCut || 
    this->current_boundary_decision_ == BoundaryDecision::BlackFade) {
    // 立即旁路跳过 NVOFA 运动估计与双向 Warp 混合
    for (float t : interpolation_timestamps) {
        CUdeviceptr target_buffer = GetOutputSlot(t);
        if (t < 0.5f) {
            cudaMemcpyAsync((void*)target_buffer, (void*)frame0_ptr, frame_size_, cudaMemcpyDeviceToDevice, stream);
        } else {
            cudaMemcpyAsync((void*)target_buffer, (void*)frame1_ptr, frame_size_, cudaMemcpyDeviceToDevice, stream);
        }
    }
    // 标记当前镜头发生切镜，通知下游
    NotifyDownstreamSceneCut(stream);
    return;
}
```

#### **Step 2.2：新镜头首帧 NVOFA 硬件历史提示复位**

在 `bridge_exports.cpp:674-723`（NVOFA 执行接口）及 `nvof_engine.cpp` 中新增标志控制：

```
// 在 NV_OF_EXECUTE_INPUT_PARAMS 填充逻辑中
NV_OF_EXECUTE_INPUT_PARAMS evalParams = {};
if (bResetTemporalHints) {
    // 强制硬件光流引擎清零上一镜头的残余运动矢量缓冲
    evalParams.disableExternalHints = NV_TRUE; // 或对应 D3D12 / CUDA 光流 API 标志位
}
```

每当切镜/黑场发生后的第一个正常镜头内帧对（Continuous）送入 NVOFA 时，显式置位 `bResetTemporalHints = true`，随后清零该标志。

#### **Step 2.3：镜头内前景遮挡双向闭环一致性校验**

在 NVOFA 合成 Warp CUDA Kernel（位于 `nvof_engine.cpp`）中追加对称残差校验与像素亮度差判定：

\$\$\\text{OcclusionError} \= \|\\vec{V}{\\text{fwd}}(x,y) \+ \\vec{V}{\\text{bwd}}(x \+ V\_{\\text{fwd},x}, y \+ V\_{\\text{fwd},y})\|*2 \+ \\alpha \\cdot \|I\_0(x,y) \- I\_1(x \+ V*{\\text{fwd},x}, y \+ V\_{\\text{fwd},y})\|\$\$

* **退回机制**：当 \$\\text{OcclusionError} \> \\tau\_{\\text{occ}}\$ 时，判断该像素属于遮挡或新暴露区域，取消 \$I\_0\$ 与 \$I\_1\$ 的双向加权融合，自动降级为可见侧单向像素采样；  
* **消除效应**：彻底解决高速运动物体边缘拉丝、果冻气泡与边缘畸变。

#### **Step 2.4：下游 VSR 时域 Reset 与编码器 IDR 同步**

当检测到切镜事件（`HardCut` 或 `BlackFade`）时：

1. 向 RTX Video Super Resolution (VSR) 管线投递标记：`vsr_params.force_reset_temporal = true`；  
2. 向 NVENC 编码器管线发送信号，强制在下一帧注入 `IDR` 关键帧（Keyframe）。

---

## **4\. 法定验收门禁**

验收必须在 **RTX 5060 真机环境** 上运行并通过以下断言：

### **4.1 测试集构建 (`tests/test_nvof_scene_cut.cpp`)**

将该单元测试集成至主工程自动化测试集 `rtx_tests.exe`，覆盖三组典型场景：

1. **高反差硬切帧对**：从极亮场景突变至暗色场景；  
2. **渐变黑场帧对**：包含 Fade-to-Black 与 Black-Fade-In 连续帧序列；  
3. **镜头内高速平移帧对**：摄像机快速 Pan/Tilt，包含高频纹理背景。

### **4.2 严格断言指标**

p  
// 自动化测试核心断言示例  
TEST\_CASE("NVOFA Scene Cut and Anti-Jelly Validation", "\[nvof\]\[rework-wave2b\]") {  
TestFixtureContext ctx \= InitRTX5060Environment();

```
// 断言 1: 硬切与黑场处 NVOFA 输出与近端源帧 PSNR > 45 dB (无跨镜头残留混合)
float psnr_t03 = CalculatePSNR(ctx.GetOutputFrame(0.3f), ctx.GetSourceFrameI0());
float psnr_t07 = CalculatePSNR(ctx.GetOutputFrame(0.7f), ctx.GetSourceFrameI1());
REQUIRE(psnr_t03 > 45.0f);
REQUIRE(psnr_t07 > 45.0f);

// 断言 2: ShotGroupSegmenter 镜头计数 100% 准确
REQUIRE(ctx.segmenter.GetDetectedCutCount() == ctx.expected_cuts);

// 断言 3: 整体框架零失败
REQUIRE(RunAllRTXTests() == 0);
```

}

\+-------------------------------------------------------------------------------+  
\|                      RTX 5060 HARDWARE ACCEPTANCE PASS                        \|  
\+------------------------------------+------------------------------------------+  
\| Checkpoint                         \| Metric / Target                          \|  
\+------------------------------------+------------------------------------------+  
\| Scene Cut Frame Synthesis PSNR     \| \> 45.0 dB (Zero cross-shot ghosting)     \|  
\| ShotGroupSegmenter Accuracy        \| 100% Precision / Recall                  \|  
\| Unified CUDA Temporal Detector     \| \< 0.12 ms overhead @ 4K                  \|  
\| Host-Device Memory Transfer        \| 0 Bytes CPU Readback (Stream Native)     \|  
\| Test Suite Status                  \| rtx\_tests.exe: 0 FAILED                  \|  
\+------------------------------------+------------------------------------------+