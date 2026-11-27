# **18\_rework\_wave3-A\_实时60fps\_AI增强播放器与WASAPI同步施工单**

## **1\. 元信息与驳回定性**

### **1.1 返工波次信息**

* **波次标识**：Rework Wave 3-A  
* **前置依赖项目**：依赖 Rework Wave 0-A（1001 时基彻底修复）与 Rework Wave 2-A/B（AI 推理引擎与统一场景切分算子组）全面通过验收后启动。  
* **并行任务**：与 Rework Wave 3-B（极速离线转码导出管线）并行执行。

### **1.2 驳回定性复盘 (CINERTX-P13-RULING-013)**

原 Wave 3-A（08 号文档）经协调负责人裁决定性为 **"破坏主干 \+ 空壳（REJECTED）"**，严重背离工程质量要求，主要问题归纳如下：

1. **时基破坏**：工程师在未完成 1001 精确时基重构的前提下改动半截代码，导致全套播放器与转码管线时钟基准崩溃。  
2. **AI 功能空壳化**：`MediaPlaybackSession`（`src/core/playback_session_video.cpp:151-167` 及 `src/core/ngx_remaster.cpp:111-203`）代码块中，对于 VSR（超分辨率）、TrueHDR（高动态范围重建）、3D LUT（专业调色）以及 RIFE/NVOFA（60fps 实时帧率倍增）的接入率均为 **0%**，实时增强仅处于虚构状态。  
3. **GUI 逻辑悬空**：`src/gui/workstation_canvas.h:79-88` 中的 `active_vsr_target()` 仅执行了简单的 OSD 文本渲染打印，未将参数下发至硬件解码与 TensorRT 渲染管线，造成界面配置与底层播放解耦悬空。

### **1.3 宪法级硬红线**

* **零类型膨胀规约**：执行 `AuditTypes <= 34/35`。严禁新增任何具名 `struct` 或 `class`。必须完全复用已有的 `PlaybackSession` 私有原子字段与无状态自由函数。  
* **Zero On-the-Fly Compilation \[DEC-011\]**：播放器在初始化完成后以及运行期间，绝对禁止触发任何 TensorRT 现场编译（On-the-fly Compilation）或 Kernel 构建操作。所有 Engine 必须在初始化阶段预热并完成序列化加载。

---

## **2\. Task 1：彻底废除 `createHeadlessPreviewSink` 旁路，打通 GPU 实时增强管线**

### **2.1 精确修改坐标**

* `src/core/playback_session_video.cpp:151-167`  
* `src/core/ngx_remaster.cpp:111-203`  
* `src/gui/workstation_canvas.h:79-88`

### **2.2 执行步骤 (TDD)**

#### **Step 1.1：全链路 GPU 增强节点接入**

全面废除原始帧直通旁路 `createHeadlessPreviewSink`。重构 `playback_session_video.cpp` 的渲染主路径，将解码获得的 NV12/P010 GPU Surface 统一送入与转码器同源的统一算子管线。管道流程严格按照如下拓扑顺序执行：

```
[NVDEC / SW-Upload]
         │
         ▼
[Unified SceneCutGuard (Wave 2-B)]
         │
         ▼
[FRUC (RIFE 4-Bucket / NVOFA 60fps 分数升帧)]
         │
         ▼
[VSR (视口感知动态目标分辨率)]
         │
         ▼
[TrueHDR (SDR -> HDR10 PQ)]
         │
         ▼
[3D LUT Color Transform]
         │
         ▼
[A/B Split-Screen CUDA Kernel]
         │
         ▼
[Latest-Frame Mailbox]
         │
         ▼
[D3D12Presenter (Direct Composition / SwapChain)]
```

#### **Step 1.2：消除全局悬空函数，落地视口感知代理降载 (Proxy Scaling)**

1. **清理悬空接口**：彻底删除 `src/gui/workstation_canvas.h:79-88` 中的全局静态函数 `active_vsr_target()`。  
2. **原子槽位同步**：在 `PlaybackSession` 内部维护以下 `std::atomic` 字段，由 GUI 线程通过无锁信道实时更新：  
   * `std::atomic<uint32_t> target_viewport_w_`  
   * `std::atomic<uint32_t> target_viewport_h_`  
   * `std::atomic<float> split_screen_ratio_`  
3. **动态视口适配与代理降载**：在帧边界安全点（Frame-boundary Safe Point）依据当前 GUI 视口物理像素尺寸计算等比超分目标分辨率，严禁超分输出超出窗口实际渲染区域。若输入为 4K 视频，且在 `16.67ms` 的 60fps 实时周期内算力吃紧，算子调度器自动激活 Proxy Scaling 代理降载策略，将 VSR 目标尺寸降低一级（例如降至 `1080p -> 1440p` 代理），优先保障 60fps 帧率平稳。

#### **Step 1.3：真·实时 A/B 分屏对比 CUDA Kernel**

在增强管线末端，硬件管线需同时保留同一时间戳的"未增强原始帧（经色彩空间转换至 RGB10/FP16）"与"全套 AI 增强后帧"。根据 `split_screen_ratio_` 在单个 CUDA Kernel 中完成合成：p  
// Kernel 逻辑伪码：单 Pass 完成 A/B 分屏与准星分割线合成  
**global** void RenderABSplitKernel(  
const cudaSurfaceObject\_t srcRawRGB,  
const cudaSurfaceObject\_t srcEnhancedRGB,  
cudaSurfaceObject\_t dstSurface,  
int width, int height, float splitRatio)  
{  
int x \= blockIdx.x \* blockDim.x \+ threadIdx.x;  
int y \= blockIdx.y \* blockDim.y \+ threadIdx.y;

```
if (x >= width || y >= height) return
```

| ; |
| :---- |

```
int splitBoundary = __float2int_rn(width * splitRatio);
ushort4 pixelColor;

if (abs(x - splitBoundary) <= 1) {
    // 2px 白色准星分割线
    pixelColor = make_ushort4(65535, 65535, 65535, 65535);
} else if (x < splitBoundary) {
    // 左侧：原始帧
    pixelColor = surf2Dread<ushort4>(srcRawRGB, x * sizeof(ushort4), y);
} else {
    // 右侧：AI 增强帧
    pixelColor = surf2Dread<ushort4>(srcEnhancedRGB, x * sizeof(ushort4), y);
}
surf2Dwrite(pixelColor, dstSurface, x * sizeof(ushort4), y)
```

;  
}

````

---

## 3. Task 2：精确有理数 60fps 节拍器、WASAPI 锁相与 8ms 熔断

### 3.1 NTSC `1001` 精确有理数时间槽映射
基准时间轴统一采用 Wave 0-A 规定的 `706,305,600,000 Hz` `Flicks` 时间单位。对于任意帧率转换（如 23.976fps -> 60fps），按如下有理数公式映射输出显示相位：

$$\text{pos} = k \times \frac{F_{in}}{F_{out}}$$

其中 $k$ 为输出 60fps 序列的帧索引。

```cpp
// 帧率转换相位与算子选择逻辑
constexpr double EPSILON = 1e-3;
double pos = out_frame_idx * (in_frame_rate / 60.0);
int base_idx = static_cast<int>(std::floor(pos));
double alpha = pos - base_idx;

if (std::abs(alpha) < EPSILON) {
    // 零插帧：相位重合，直接透传源帧 I_base
    ExecuteDirectPass(frame_buffer[base_idx]);
} else if (std::abs(alpha - 1.0) < EPSILON) {
    ExecuteDirectPass(frame_buffer[base_idx + 1]);
} else {
    // 相位不重合：驱动 RIFE / NVOFA 进行分数相位中间帧合成
    // 典型 23.976 -> 60fps 循环相位 α 集合: { 0.0, 0.4, 0.8, 0.2, 0.6 }
    ExecuteFrameInterpolation(frame_buffer[base_idx], frame_buffer[base_idx + 1], static_cast<float>(alpha));
}
````

## **3.2 WASAPI 音频主时钟主导与 8.0ms 死线熔断**

硬件播放主时钟严格由 `WasapiAudioOutput` 设备的底层 Audio Position 掌控。视频渲染队列不得反压阻塞音频硬件 Buffer。

```
                       [WASAPI 硬件主时钟]
                                │
                                ▼
                       PTS_Audio_Current
                                │
                                ▼
  [解码/推理异步环] ──► PTS_Video_Target ──► 误差比对 (PTS_Video - PTS_Audio)
                                                │
                       ┌────────────────────────┴────────────────────────┐
                       ▼                                                 ▼
            Delay <= 8.0 ms                                    Delay > 8.0 ms
       [完整执行 AI 增强管线]                               [激活单帧 AI 旁路熔断]
               │                                                 │
               ▼                                                 ▼
         上屏 AI 增强帧                                丢弃当前推理，直出最近解码原帧
                                                       ai_bypass_count.fetch_add(1)
```

1. **预读环（Lookahead Ring Buffer）**：解码与 TensorRT 推理线程在独立的异步线程中运行，维护 4\~6 帧的有界预读环。  
2. **死线判定（Deadline Enforcement）**：渲染线程在准备提交 D3D12 Present 前，获取当前 WASAPI 硬件播放 PTS（\$\\text{PTS}*{audio}\$）与待上屏视频帧 PTS（\$\\text{PTS}*{video}\$）：  
   \$\$\\Delta t \= \\text{PTS}*{video} \- \\text{PTS}*{audio}\$\$  
   若 \$\\Delta t \< \-8.0\\text{ ms}\$（即视频推理严重滞后，已错过最佳渲染窗口），**立即触发单帧 AI 旁路熔断**：跳过本帧的 RIFE/VSR TensorRT 推理，直接将上一帧已解码的 Raw GPU Surface 提交上屏，并执行 `ai_bypass_count.fetch_add(1, std::memory_order_relaxed)` 记录日志，保证音频流绝不出现咔哒声（Pops/Clicks）或中断。

---

# **4\. Task 3：`Seek` / `Pause` / `StepFrame` / `Speed` 时域缓存原子重置**

在用户触发 Seek、反向逐帧 `stepFrame(-1)` 或变速播放切换时，时域增强算子（如 RIFE 帧间参考、VSR 时域运动补偿）若保留历史帧，会导致画面出现上一镜头的残影闪烁。必须按以下序列执行无锁原子重置：

| 操作事件 | 状态清空与重置流程 | 首帧响应要求 |
| :---- | :---- | :---- |
| **`Seek(target_pts)`** | 1\. 挂起预读线程2. 清空 4\~6 帧预读环队列3. 调用 `ShotGroupSegmenter::Reset()`4\. 向 RIFE/NVOFA/VSR 下发 `force_reset_temporal = true`5\. 恢复预读线程 | 提取 `target_pts` 关键帧，**仅执行静态单帧增强**（VSR \+ TrueHDR \+ 3D LUT），放弃 RIFE 帧率倍增，并在 16.67ms 内将首帧推送到 D3D12Presenter。 |
| **`StepFrame(-1)`** | 1\. 重置预读环2. `force_reset_temporal = true`3\. 清空时域 Buffer | 准确定位至前一 GOP 对应帧，直接以静态单帧增强模式上屏。 |
| **`Speed(0.25x~2.0x)`** | 1\. 重新计算分数节拍器步进值 \$\\Delta \\text{pos} \= \\frac{F\_{in} \\cdot \\text{Speed}}{60.0}\$2. 保持时域 Buffer 连续性但更新时间步长权重 | 保持平滑过渡，防范帧率转换失帧。 |

---

# **5\. 法定验收门禁 (RTX 5060 真机实测)**

本施工单必须在指定的物理机硬件环境（NVIDIA GeForce RTX 5060）下通过自动化测试套件与物理播放校验，方可合并进主干。

## **5.1 门禁测试矩阵**

| 序号 | 测试项 | 动作与输入源 | 判定合格标准 |
| :---- | :---- | :---- | :---- |
| **PASS-01** | 长时间同步稳定度测试 | 23.976fps H.264 / HEVC / AV1 实片，开启 RIFE 60fps \+ VSR \+ TrueHDR \+ 3D LUT \+ A/B 分屏，连续播放 60 秒。 | 1\. `A/V Drift` 偏差绝对值全局保持在 \$\\le \\pm 16.67\\text{ ms}\$ 以内。2. `ai_bypass_count` 增加次数不超过总帧数的 0.1%。 |
| **PASS-02** | 极速 Seek 与逐帧压力测试 | 自动化脚本连续执行 50 次随机 `Seek` \+ `StepFrame(+1/-1)` \+ `Speed(0.25x - 2.0x)` 突发切换。 | 1\. 全程无死锁、无程序崩溃、无音轨音频蜂鸣（Buzzing）。2. 每次 Seek 后首帧渲染无上一镜头残留画面（Ghosting/Flashing）。 |
| **PASS-03** | 自动化单元与集成测试 | 执行全套测试套件 `test_playback_session.cpp`。 | **0 FAILED**，全部断言通过。 |

