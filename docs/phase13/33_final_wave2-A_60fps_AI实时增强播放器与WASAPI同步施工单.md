# **33\_final\_wave2-A\_60fps\_AI实时增强播放器与WASAPI同步施工单**

## **1\. 波次定位与宪法红线**

### **1.1 波次依赖与定位**

* **波次代码**：`Final Wave 2-A`  
* **依赖前置**：必须在 `31_final_wave0-A`（宿主机单测全绿）与 `32_final_wave1-A`（转码与导出管线全通）完工验收后启动。  
* **核心目标**：彻底废除 `MediaPlaybackSession` 中的原始帧直通旁路（`createHeadlessPreviewSink`），将与导出管线完全同源的 CUDA/NGX/TRT 增强节点接通至实时播放链路，实现 `23.976 -> 60fps` 实时分数升帧、VSR、TrueHDR、3D LUT、A/B 分屏合成与 WASAPI 音频锁相同步。

### **1.2 宪法红线规约**

* **红线 1（类型总数严格受控）**：`AuditTypes <= 34/35`。本波次整改**严禁新增任何具名 `struct` 或 `class`**。所有新增状态与视口参数必须复用 `PlaybackSession` 内部已有私有 `std::atomic` 槽位，或者通过无状态自由函数（Free Functions）完成计算。  
* **红线 2（零现场编译 `[DEC-011]`）**：播放器在播放、Seek、切分辨率或调整窗口大小时，**严禁触发任何 TensorRT 现场编译/重构建**。必须直接复用 RTX 5060 宿主机上已预构建好的 `ultra / wide / square / tall` 四桶单 Profile `.engine` 离线模型文件。  
* **红线 3（测试静音隔离）**：自动化单测与集成测试期间保持 `null_sink = true` 静音隔离模式，严禁产生任何 440Hz 试音音频输出或触发 `MessageBeep` 系统蜂鸣。

---

## **2\. Task 1：废除原始帧直通旁路，打通全链路 GPU 实时增强**

### **2.1 精准代码坐标**

* `src/core/playback_session_video.cpp:151-167`  
* `src/core/ngx_remaster.cpp:111-203`  
* `src/gui/workstation_canvas.h:79-88`  
* `src/gui/rtx_gui.cpp:266-286`

### **2.2 Step 1.1：统一 GPU 增强节点全链路接入**

废除 `createHeadlessPreviewSink` 的 raw frame 旁路逻辑。重构 `playback_session_video.cpp`，使解码后的 GPU 帧按顺序通过以下流水线处理：

```
NVDEC/SW-Upload
   │
   ▼
Unified SceneCutGuard (8x8 SAD + 黑场检测)
   │
   ▼
FRUC (RIFE 4-Bucket / NVOFA 60fps 分数升帧)
   │
   ▼
VSR (视口感知动态目标分辨率)
   │
   ▼
TrueHDR (SDR -> HDR10 PQ 映射)
   │
   ▼
3D LUT (色彩空间微调与 Style 应用)
   │
   ▼
A/B Split-Screen CUDA Kernel (合成 2px 白线分割对比帧)
   │
   ▼
Latest-Frame Mailbox (原子环形帧邮箱)
   │
   ▼
D3D12Presenter / GUI Viewport
```

### **2.3 Step 1.2：消除全局悬空 `active_vsr_target()`，落地视口感知代理降载**

1. 删除 `src/gui/workstation_canvas.h:79-88` 中定义的全局静态 `active_vsr_target()` 函数。  
2. 在 `PlaybackSession` 类定义内部新增原子字段（不增加具名类型）：  
   * `std::atomic<uint32_t> target_viewport_w_{0};`  
   * `std::atomic<uint32_t> target_viewport_h_{0};`  
   * `std::atomic<float> split_screen_ratio_{0.5f};`  
   * `std::atomic<uint64_t> active_feature_mask_{0};`  
3. GUI 视口发生 Size 变化或拖拽分屏滑块时，仅写上述原子字段。  
4. 播放线程在帧边界安全点计算等比 VSR 目标分辨率：若 4K 源帧在 `16.67 ms` 周期内算力吃紧，根据当前 Viewport 大小自动降低 VSR 目标桶尺寸（Proxy Scaling），优先保 60fps 帧率稳健。

### **2.4 Step 1.3：真·实时 A/B 分屏对比 CUDA Kernel**

在 GPU 增强管线末端，保留同一 PTS 时间戳对应的"未增强源帧（NV12/P010 转 RGB10）"与"全链路 AI 增强帧"。在单独的 CUDA Kernel (`k_BuildSplitScreenComparison`) 中完成裁切合成：

\$\$\\text{Output}(x, y) \= \\begin{cases} \\text{Frame}*{\\text{Src}}(x,y), & \\text{if } x \< X*{\\text{split}} \- 1 \\ \\text{RGB10}(1.0, 1.0, 1.0), & \\text{if } \|x \- X\_{\\text{split}}\| \\le 1 \\ \\text{Frame}*{\\text{Enhanced}}(x,y), & \\text{if } x \> X*{\\text{split}} \+ 1 \\end{cases}\$\$  
\|---\|  
其中 \$X\_{\\text{split}} \= \\text{round}(W \\times \\text{split\_screen\_ratio\_})\$。

---

## **3\. Task 2：60fps 有理数节拍器、WASAPI 锁相与 8ms 熔断机制**

### **3.1 Step 2.1：NTSC `1001` 有理数分数相位映射**

在统一的 `706,305,600,000 Hz` (`Flicks`) 高精度时基上，精确支持 `23.976 -> 60fps` 等复杂帧率转换：

```
// 在 PlaybackSession 私有计算函数中
const uint64_t ticks_per_src_frame = FLICKS_PER_SEC * in_den / in_num;
const uint64_t ticks_per_out_frame = FLICKS_PER_SEC * out_den / out_num;

uint64_t target_out_tick = out_frame_idx * ticks_per_out_frame;
double exact_src_pos = static_cast<double>(target_out_tick) / ticks_per_src_frame;

uint64_t frame_idx0 = static_cast<uint64_t>(std::floor(exact_src_pos));
uint64_t frame_idx1 = frame_idx0 + 1;
float alpha = static_cast<float>(exact_src_pos - frame_idx0);

if (std::abs(alpha) < 1e-3f) {
    // 相位零点，零插帧直出源帧 frame_idx0
    ExecuteDirectPass(frame_idx0);
} else {
    // 驱动 RIFE / NVOFA 结合 alpha 参数合成中间帧
    // 对于 23.976 -> 60fps，精确循环 alpha 相位：[0.0, 0.4, 0.8, 0.2, 0.6]
    ExecuteFRUCInterp(frame_idx0, frame_idx1, alpha);
}
```

### **3.2 Step 2.2：WASAPI 主时钟锁相与 `8.0 ms` 死线熔断**

1. **异步双线程架构**：  
   * **解码与 AI 推理线程**：以 4\~6 帧有界预读环（Ring Buffer）异步生产增强帧。  
   * **渲染与呈现线程**：以 WASAPI 硬件播放主时钟为基准时基进行对齐渲染。  
2. **`8.0 ms` 熔断决策算法**：

```
double audio_pts = g_wasapi_output.GetHardwareClockPTS();
double video_pts = current_frame.pts;
double latency_ms = (video_pts - audio_pts) * 1000.0;

if (latency_ms < -8.0) {
    // AI 推理滞后超过 8.0ms 死线，触发单帧 AI 旁路熔断
    ai_bypass_count.fetch_add(1, std::memory_order_relaxed);
    PresenterDropOrPassRawFrame(current_frame.raw_passthrough_gpu_tex);
} else {
    PresenterSubmitEnhancedFrame(current_frame.enhanced_gpu_tex);
}
```

确保音视频绝对同步，坚决防范由于 AI 计算波动反压阻塞音频流。

---

## **4\. Task 3： Seek / Pause / Step / Speed 时域缓存无残影重置**

当用户执行 Seek、反向单帧步进、暂停或调整播放倍速时，必须原子化清理时域缓存，防范图像拖尾残影。

### **4.1 操作响应矩阵与动作规约**

| 操作指令 | 预读环状态 | 时域 Segmenter 动作 | TensorRT / NGX 状态 | 渲染输出行为 |
| :---- | :---- | :---- | :---- | :---- |
| `seek(pts)` | `RingBuffer::Reset()` | `ShotGroupSegmenter::Reset()` | `force_reset_temporal = true` | 对 Seek 目标帧执行静态单帧增强（VSR \+ HDR \+ LUT）后立即上屏 |
| `stepFrame(-1)` | `RingBuffer::Reset()` | `ShotGroupSegmenter::Reset()` | `force_reset_temporal = true` | 提取上一关键帧完整解码至目标帧，无插帧上屏 |
| `stepFrame(+1)` | 保留当前帧 | 保持追踪 | 增量推理 1 帧 | 静态增强单帧呈现 |
| `speed(0.25~2.0)` | 刷新有界环 | 重新计算 Flicks 步进比率 | 重置相位计数器 | 依新有理数相位映射运行 |

```
// seek(pts) 执行示例
void PlaybackSession::ExecuteSeek(int64_t target_pts) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    ring_buffer_.Clear();
    shot_segmenter_.Reset();
    
    // 强制下发时域重置标记
    rife_engine_.SetTemporalReset(true);
    vsr_engine_.SetTemporalReset(true);
    
    // 解码 Seek 首帧
    GPUFrame raw_frame = decoder_.SeekAndDecode(target_pts);
    
    // 执行静态单帧 AI 增强
    GPUFrame enhanced_frame = ApplyStaticEnhance(raw_frame); 
    
    presenter_.PresentImmediately(enhanced_frame);
}
```

---

## **5\. 宿主机真机验收 Gate**

施工完成后，必须执行以下自动化与真机集成测试验证：

```
// test_playback_ai_pipeline.cpp 并入 rtx_tests.exe
TEST_CASE("Playback AI Pipeline Integration & Bypass Safeguard", "[wave2a][rtx5060]") {
    PlaybackSession session;
    session.SetHeadless(true); // 保持 null_sink 静音隔离
    
    REQUIRE(session.LoadMedia("test_23976fps_4k.mp4") == true);
    
    // 开启全套增强
    session.EnableVSR(true);
    session.EnableTrueHDR(true);
    session.EnableFRUC(true); // 23.976 -> 60fps
    session.SetSplitScreenRatio(0.5f);
    
    session.Play();
    
    // 运行 60 秒模拟逻辑
    for (int i = 0; i < 3600; ++i) {
        session.TickRenderThread();
        
        // 校验 AV Drift
        double drift = session.GetAudioVideoDriftMs();
        REQUIRE(std::abs(drift) <= 16.67);
        
        // 随机触发 Seek / Step
        if (i % 100 == 0) {
            session.Seek(i * 10000000ULL);
            REQUIRE(session.GetTemporalGhostingMetric() < 0.01f);
        }
    }
    
    REQUIRE(AuditTypes::Count() <= 35);
}
```

### **验收指标清单**

1. **类型计数与红线**：`AuditTypes <= 35`（零新增具名 class/struct）。  
2. **零现场编译**：全程零 `.engine` 编译日志产生，运行期间显存分配平稳。  
3. **音视频漂移**：在 RTX 5060 宿主机上 60 秒连续播放，`A/V Drift` 稳定保持在 `±16.67 ms` 范围内。  
4. **操作稳健性**：连续 50 次切换 `Seek / Step / Speed`，零死锁、零画面鬼影/残影，`ai_bypass_count` 仅在故意引入延迟注入时按预期递增。  
5. **测试命令**：`./rtx_tests.exe [wave2a]` 结果为 `ALL PASS`。

