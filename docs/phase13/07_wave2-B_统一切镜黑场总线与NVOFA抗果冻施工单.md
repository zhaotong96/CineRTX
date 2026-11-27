# **Wave 2-B：统一切镜黑场总线与 NVOFA 抗果冻施工单**

## **1\. 施工基本信息与宪法级限制规约**

本施工单属于系统迭代 **Wave 2 \- Stage B** 阶段，与 Wave 2-A（硬件编解码流水线零拷贝打通）并行推进。目标为建立全链路统一 GPU 切镜/黑场/淡入淡出检测原语，彻底消灭 NVOFA（NVIDIA Optical Flow Accelerator）插帧路径下的果冻/融化（Jelly/Melting）拖影问题，同时激活并完整整合系统内孤悬的 ShotGroupSegmenter 状态机。

### **核心宪法红线**

* **类型配额严格守恒**：全工程具名类型配额严格保持在 `AuditTypes <= 34/35`（当前硬性上限）。禁止新增任何具名 `struct`、`class` 或 `enum`。所有新增分析状态必须复用既有 `ShotGroupSegmenter` 类及其私有 POD 结构，或通过自由函数（Free Functions）、`std::tuple`/`std::pair` 及无类型 CUDA 设备内存缓冲区进行传递。  
* **Zero CPU Byte Readback**：全程禁止任何像素级与直方图级的 CPU 内存回读（D2H）。统计归约（8x8 Block SAD、Luma 均值/方差、16-bin 直方图）必须完全在 CUDA Stream 内以单次 Kernel Grid 完成，仅允许将压缩后的 64-byte 标量结构写入 Pinned/Device Memory 用于状态转移判决。  
* **严格线程与 Stream 同步机制**：切镜分析 Kernel 必须挂载于解码异步 Stream（`stream_decode`），且其输出标志位必须无缝传递至 downstream 的插帧 Stream（`stream_vsr` / `stream_nvofa`），严禁阻塞主控制线程。

---

## **2\. 现存代码缺陷与底层技术机理分析**

### **2.1 缺陷代码坐标**

| 模块路径 | 代码行号 | 现存问题描述 |
| :---- | :---- | :---- |
| `src/shim/ngx_bridge/rife_engine.cpp` | `640-671` | RIFE 路径独占了 `launchEvalBlockSad8x8` 与 `evalBlockSadSceneCut` 自适应 EMA 计算，其他光流 Engine 无法共享检测结果。 |
| `src/core/hw_transcode_stages.cpp` | `423-428` | RIFE 局部判定切镜后，仅在 RIFE 内部走近端原帧拷贝，未能将切镜状态广播给全局转码上下文。 |
| `src/core/hw_transcode_stages.cpp` | `446-459` | NVOFA 调起路径完全缺乏切镜与黑场检测，盲目对硬切帧对执行 Warp 混合。 |
| `src/shim/ngx_bridge/bridge_exports.cpp` | `674-723` | NVOFA 接口层未暴露并无效化 `NV_OF_EXECUTE_INPUT_PARAMS` 的 temporal hint 标记，导致跨镜头残影。 |
| `src/core/hw_transcode_internal.h` | `242` | 声明了 `ShotGroupSegmenter segmenter_` 成员变量，但处于逻辑游离状态。 |
| `src/core/hw_transcode_stages.cpp` | `79` | 初始化阶段仅调用一次 `resetShotSegmenter`，转码主循环内完全遗漏了 `update` 调用。 |

### **2.2 底层物理与硬件机理分析**

```
[ Frame N-1 ] ──────┐
                    ├─► [ Unified Temporal Boundary Analyzer ] ──► ShotGroupSegmenter ──► BoundaryState
[ Frame N   ] ──────┘    (Single Pass CUDA Reduced Metrics)          (State Machine)      (HardCut/BlackFade)
                                                                                                │
      ┌─────────────────────────────────────────────────────────────────────────────────────────┴───────────────┐
      ▼                                                                                                         ▼
[ HardCut / BlackFade = TRUE ]                                                            [ Continuous Sequence ]
      │                                                                                                         │
      ├─► NVOFA: Bypass Flow Exec & Dual Warp ──► Nearest Frame Passthrough                                     ├─► NVOFA: Regular Execution
      ├─► NVOFA HW: Set NV_OF_EXECUTE_FLAG_INVALIDATE_DISABLE_TEMPORAL_HINTS                                   ├─► Apply Bi-directional Symmetry Check
      └─► Downstream: Set force_reset_temporal=true ──► Request NVENC IDR Frame                                 └─► Temporal Forward Warp
```

1. **NVOFA Cost Buffer 局限性**：NVOFA 硬件返回的 Cost 缓冲区反映的是 Block Match 的相对置信度，但静止画面或暗部区域的热噪声同样会产生高 Cost 波动；反之，高平滑度的跨镜头场景（如全白/全黑硬切）Cost 反而极低。因此 Cost 缓冲区不能独立作为切镜判断依据。  
2. **跨镜头矢量拉伸（果冻效应）**：NVOFA 在处理 \$I\_0 \\to I\_1\$ 画面剧变时，由于缺乏边界感知，仍会强行在无关联的两个场景间搜索运动矢量（Motion Vectors）。前后向 Warp 核函数应用这些错误的极长矢量，会导致中间帧呈现融化、严重形变与像素拉伸。  
3. **硬件 Temporal Hint 污染**：NVOFA 内部保留了 Temporal Vector Predictor 硬件缓存。在硬切发生后的第 \$N+1\$ 帧，若不显式通过 `NV_OF_EXECUTE_INPUT_PARAMS` 标记无效化历史提示，硬件引擎将继续施加上一镜头的矢量预测，造成切镜后 2\~3 帧持续产生拖尾残影。  
4. **渐变黑场/白场（Fade Boundary）失效**：传统的 SAD 检测在渐变黑场中，由于相邻帧变化率小于自适应阈值而失效。但黑场中极低对比度（低方差）会导致光流矢量的方向混乱，需配合方差 \$\\sigma\_Y^2\$ 及直方图 L1 距离联合判定。

---

## **3\. 规约化执行步骤 (TDD)**

### **Task 1：扩展 CUDA 统一时域边界探测原语 (Unified Temporal Boundary Analyzer)**

#### **A. 代码实现与分析器扩展**

在 `src/cuda/temporal_boundary_analyzer.cu` 中实现合并式 CUDA Kernel。单次 Kernel 调度并发计算三项标量指标，并写入 64-byte 结构的 Pinned Host Memory (`BoundaryMetricsPOD`) 中：

1. **`block_sad_ema_ratio`**：将 1080p/4K 图像划分为 \$8 \\times 8\$ 网格，计算对应 Luma 块的绝对差值和（SAD），并归约出全图 SAD 均值与自适应滑动平均（EMA）的比值：  
   \$\$R\_{\\text{SAD}} \= \\frac{\\text{SAD}*{\\text{current}}}{\\text{EMA}*{\\text{SAD}} \+ \\epsilon}\$\$  
2. **`luma_mean_and_var`**：并行归约计算当前帧与前一帧的 Luma 均值 \$\\mu\_Y\$ 与方差 \$\\sigma\_Y^2\$：  
   \$\$\\mu\_Y \= \\frac{1}{N}\\sum\_{i=1}^N Y\_i, \\quad \\sigma\_Y^2 \= \\frac{1}{N}\\sum\_{i=1}^N (Y\_i \- \\mu\_Y)^2\$\$  
3. **`luma_hist_diff`**：利用 Shared Memory 原子操作构建 16-bin 归一化直方图 \$H\_0, H\_1\$，并计算其 L1 距离：  
   \$\$D\_{\\text{hist}} \= \\frac{1}{2} \\sum\_{b=0}^{15} \|H\_0\[b\] \- H\_1\[b\]\|\$\$

```
// 严格复用现有数据结构，无新增具名类型
// 判定逻辑插入至 src/core/hw_transcode_stages.cpp
void analyze_temporal_boundary_gpu(
    const uint8_t* __restrict__ frame_prev_y,
    const uint8_t* __restrict__ frame_curr_y,
    int width, int height, int pitch,
    float* d_metrics_output, // 指向 16 个 float 的设备/固定内存
    cudaStream_t stream) 
{
    // 布局：[0]=SAD_Ratio, [1]=Mean_Y0, [2]=Var_Y0, [3]=Mean_Y1, [4]=Var_Y1, [5]=Hist_L1, [6..15]=Reserved
    dim3 block(256);
    dim3 grid((width + 31) / 32, (height + 31) / 32);
    // 执行 Kernel 归约...
}
```

#### **B. 激活 `ShotGroupSegmenter` 状态机**

在 `src/core/hw_transcode_stages.cpp` 的解码阶段后、分流至 RIFE/NVOFA 之前，按如下逻辑更新 `segmenter_`：

```
// 每帧解码完成后必须触发
bool is_black_fade = (var_y0 < 4.0f || var_y1 < 4.0f) && std::abs(mean_y0 - mean_y1) > 12.0f;
bool is_hard_cut = (sad_ratio > 3.5f) && (hist_l1_diff > 0.35f);

ShotBoundaryType boundary = ShotBoundaryType::Continuous;
if (is_hard_cut) {
    boundary = ShotBoundaryType::HardCut;
} else if (is_black_fade) {
    boundary = ShotBoundaryType::BlackFade;
}

// 盘活 segmenter_
segmenter_.updateState(frame_index, boundary);
```

---

### **Task 2：NVOFA 硬切退避、硬件 Temporal Hint 复位与 downstream 联动**

#### **A. 边界帧硬切 Bypass (Zero-Jelly Bypass)**

在 `src/core/hw_transcode_stages.cpp:446-459` 中，替换原 NVOFA 调起逻辑：

```
const bool requires_bypass = (segmenter_.currentBoundary() != ShotBoundaryType::Continuous);

if (requires_bypass) {
    // 零果冻退避策略：完全跳过光流计算与双向 Warp
    for (int i = 0; i < interp_factor - 1; ++i) {
        float t = static_cast<float>(i + 1) / interp_factor;
        const uint8_t* src_passthrough = (t < 0.5f) ? frame_i0_ptr : frame_i1_ptr;
        cudaMemcpyAsync(output_interp_ptrs[i], src_passthrough, frame_bytes, 
                        cudaMemcpyDeviceToDevice, stream_nvofa);
    }
    // 标记下一个非切镜帧必须强制复位 NVOFA 硬件 Hint
    nvofa_hardware_hint_reset_pending_ = true;
} else {
    // 正常执行 NVOFA 光流与 Warp
    dispatch_nvofa_execution(frame_i0_ptr, frame_i1_ptr, nvofa_hardware_hint_reset_pending_);
    nvofa_hardware_hint_reset_pending_ = false;
}
```

#### **B. NVOFA 硬件 Temporal Hint 清零**

修改 `src/shim/ngx_bridge/bridge_exports.cpp:674-723`，将复位标志透传至 NVIDIA Optical Flow SDK 底层结构体：

```
// 映射 NV_OF_EXECUTE_INPUT_PARAMS
NV_OF_EXECUTE_INPUT_PARAMS executeParams = {};
memset(&executeParams, 0, sizeof(executeParams));

if (disable_temporal_hints) {
    // 强制关闭并清空 temporal predictor 内存
    executeParams.disableExternalHints = NV_OF_TRUE;
    // 禁用硬件 temporal predictor 缓存参考
    executeParams.flags |= NV_OF_EXECUTE_FLAG_INVALIDATE_DISABLE_TEMPORAL_HINTS;
}
```

#### **C. Downstream VSR 与 NVENC 协同**

当 `requires_bypass == true` 时，同步更新管道上下文标志：

1. **VSR 历史重置**：调用 downstream VSR 模块的 `reset_temporal_context()`，清除 RTX-VSR 或自定义 Super-Resolution 的帧历史队列。  
2. **NVENC IDR 注入**：向编码器 Stage 发送信号，设置 `NV_ENC_PIC_PARAMS::encodeConfig.gopLength = IDR_FORCED`，强制在切镜位置写入 IDR 帧，防止下游播放器拖影并提升 GOP 切割效率。

---

### **Task 3：镜头内前景遮挡反局部果冻防护 (Bidirectional Flow Consistency Check)**

在 NVOFA 合成 Warp CUDA Kernel（`src/cuda/nvofa_warp_kernel.cu`）中，增加轻量化前向与后向矢量闭环一致性检验及像素亮度差约束：

```
__device__ inline float check_flow_consistency(
    float2 fwd_flow, float2 bwd_flow,
    float4 pixel_i0, float4 pixel_i1_warped) 
{
    // 1. 矢量逆向对称校验 ||V_fwd + V_bwd||^2
    float flow_err_sq = (fwd_flow.x + bwd_flow.x) * (fwd_flow.x + bwd_flow.x) + 
                        (fwd_flow.y + bwd_flow.y) * (fwd_flow.y + bwd_flow.y);
    float flow_mag_sq = fwd_flow.x * fwd_flow.x + fwd_flow.y * fwd_flow.y + 1e-3f;
    
    // 2. 局部光度一致性校验
    float luma_err = fabsf(pixel_i0.x - pixel_i1_warped.x);
    
    // 3. 计算遮挡权重 Alpha (0.0 表示完全遮挡/不一致，1.0 表示完全一致)
    float is_occluded = (flow_err_sq / flow_mag_sq > 0.15f) || (luma_err > 0.25f);
    return is_occluded ? 0.0f : 1.0f;
}

// 在 Warp Kernel 采样合成时：
// 若 alpha_consistency < 0.5f，自动关闭双向 Warp 混合，回退至单向 Nearest/Unoccluded 帧采样。
```

---

## **4\. 验收门禁与测试规约**

必须在 `tests/test_scene_cut_nvofa.cpp` 中覆盖以下自动化测试用例：

```
// 验收门禁测试构想与断言
TEST_CASE("NVOFA Scene Cut and Black Fade Handling", "[nvofa][scene_cut]") {
    // 1. 初始化测试上下文与数据源
    GpuTranscodePipeline pipeline;
    pipeline.initialize();

    // 2. 构造 30 帧复合测试序列：
    //    Frame 0..9  : 镜头 A (高速平移镜头)
    //    Frame 10    : 突变硬切至 镜头 B (高反差场景)
    //    Frame 11..19: 镜头 B (正常运动)
    //    Frame 20..24: 渐变至纯黑 (Fade-to-Black)
    //    Frame 25..29: 纯黑渐变恢复至 镜头 C (Fade-from-Black)
    auto test_sequence = create_synthetic_test_sequence();

    // 3. 推送序列并获取插帧输出
    std::vector<GpuFrameBuffer> outputs = pipeline.process_sequence(test_sequence);

    // 4. 断言 1: 硬切点 (Frame 10 对应的插帧) 必须为 100% 源帧拷贝，PSNR > 45dB，无混合拖影
    double psnr_hard_cut = compute_psnr(outputs[10 * interp_factor - 1], test_sequence[9]);
    REQUIRE(psnr_hard_cut > 45.0);

    // 5. 断言 2: ShotGroupSegmenter 统计镜头切换数严格等于 3 次 (HardCut=1, BlackFade=2)
    REQUIRE(pipeline.get_segmenter().get_total_shots() == 3);

    // 6. 断言 3: 检查 NVOFA 硬件 temporal hint 在 Frame 11 触发了 Invalidate 复位
    REQUIRE(pipeline.get_nvofa_engine().was_hint_invalidated_at(11) == true);

    // 7. 宪法检查: 确认类型数未超标
    REQUIRE(AuditTypesCounter::get_count() <= 35);
}
```

### **验收指标基线**

1. **PSNR 阈值**：在硬切帧与黑场渐变节点，插帧结果与最近邻源帧的 PSNR \$\\ge 45.0 \\text{ dB}\$，彻底剔除果冻与拖影。  
2. **零 CPU 占用增长**：`analyze_temporal_boundary_gpu` 运行期间，主机端 CPU 内存拷贝量增加为 **0 Bytes**。  
3. **类型配额**：通过静态代码分析，具名类型配额严格满足 `AuditTypes <= 34/35`。

