# **27\_target\_wave3-A\_异构队列MergeConcat真执行与60fps增强播放器施工单**

## **1\. 背景、前置依赖与宪法红线**

### **1.1 前置依赖履约状态**

本施工单基于已通过宿主机物理测试的 Wave 0-A、Wave 1-A、Wave 2-A 基础设施构筑，禁止修改已固化的底座 API：

* **`24_target_wave0-A`**：全链路零拷贝 D3D12 / CUDA / NVDEC 基础上下文、内存池及无锁双缓冲机制。  
* **`25_target_wave1-A`**：TensorRT 10.x 引擎执行器、引擎反序列化缓存及 `SceneCutGuard` 时域状态切断机制。  
* **`26_target_wave2-A`**：`NVDECDecoder`、`NVENCEncoder`、`WasapiAudioOutput` 硬件解复用与音频采样环动构件。

### **1.2 驳回靶点与修缮目标**

在 23 号驳回裁决中，Wave 3-B 与 Wave 3-A 被指出存在严重伪实现与悬空接口：

1. **`EncoderJobQueue` 调度虚设**：`job_queue.cpp` 仅声明了 `setJobOutputMode` / `addJobInputs` 等元数据方法，在后台工作线程的 `work_loop` 中，多文件合并转码（`MergeConcat`）与批处理导出（`BatchSeparate`）未打通真实执行路径，未真正调用 FFmpeg muxer / NVENC 管道。  
2. **播放器 AI 实时增强链断裂**：`MediaPlaybackSession` 未完整接入 VSR / TrueHDR / 3D LUT / FRUC（RIFE）60fps 实时增强渲染管线；`workstation_canvas.h:79-88` 仍残留悬空的 `active_vsr_target()` 私有空方法，导致预览视口无 AI 代理适配能力。

### **1.3 宪法级红线**

* **\[RED-LINE-01\] 零结构体膨胀规约**：全工程 `AuditTypes <= 34/35`。严禁新增任何具名 `struct` 或 `class`，所有新扩展状态必须保存在现有类私有成员字段或使用 C++ 标准库原语元组化表达。  
* **\[RED-LINE-02\] 零现场编译规约 \[DEC-011\]**：转码与播放期间严禁触发任何 TensorRT 现场编译（NVRTC/ONNX Parsing）。所有 TRT Engine 必须直接读取预编译序列化文件 `.engine`。  
* **\[RED-LINE-03\] 零磁盘中间文件与统一封装**：合并转码过程严禁向磁盘写入临时 `.yuv` / `.pcm` / `.ts` 中转文件；输出容器严格固定为封装 `.mp4`，且必须写入 `+faststart`（`moov` 头前置）。

---

## **2\. 靶向 Task 1：打通 `EncoderJobQueue` 真实执行路径**

### **2.1 任务粒度状态隔离与快照导出**

`EncoderJobQueue` 负责管理多个并发或串行转码任务。升级 `include/rtx/job_queue.h` 与 `src/core/job_queue.cpp`，实现独立配置与线程安全调度。

```
// include/rtx/job_queue.h 中的既有 EncoderJob 结构扩展 (严格遵守零新增 class/struct 红线)
enum class JobOutputMode : uint8_t { BatchSeparate = 0, MergeConcat = 1 };

struct EncoderInputSegment {
    std::string file_path;
    int64_t in_point_us{0};
    int64_t out_point_us{-1}; // -1 表示全文
};

// 在既有 EncoderJob 中充实数据项
struct EncoderJob {
    uint64_t job_id{0};
    std::vector<EncoderInputSegment> inputs;
    JobOutputMode output_mode{JobOutputMode::BatchSeparate};
    TranscodeProfile profile;
    std::string output_path;
    JobState state{JobState::Pending};
    float progress{0.0f};
};
```

1. **UI/主线程配置隔离**：  
   * 在主界面选中处于 `JobState::Pending` 状态的某条 `EncoderJob` 时，UI 检查器仅读写该 `job_id` 对应的属性。  
   * 增加 `EncoderJobQueue::update_job_inputs(uint64_t id, const std::vector<EncoderInputSegment>& inputs)` 与 `update_job_profile(uint64_t id, const TranscodeProfile& prof)`，允许在任务启动前增删调序片段。  
2. **线程拾取与不可变快照**：  
   * 当工作线程弹出任务时，在锁保护下复制当前 `EncoderJob` 的完整值快照 `EncoderJob job_snapshot`，并将任务状态置为 `JobState::Running`。  
   * 为该任务分配专属 `std::stop_token`。主线程发起 `cancel_job(id)` 时通过该 token 触发中途安全退出。

### **2.2 同构零重编极速直拼路径（Direct Stream Copy）**

在工作线程处理 `JobOutputMode::MergeConcat` 时，首先触发同构判定函数 `bool check_homogeneous_fastpath(const std::vector<EncoderInputSegment>& inputs)`。

#### **判定准则（必须同时满足）**

* 无任何 AI 滤镜（VSR/FRUC）、无色彩转换（HDR2SDR/3D LUT）且无分辨率缩放需求。  
* 所有 `inputs` 片段的视频编码格式（Codec ID）、Profile/Level、像素格式（AVPixelFormat）、分辨率（Width/Height）、Sample Aspect Ratio (SAR)、精确帧率（r\_frame\_rate）、时间基（`tbn`）完全相同。  
* 音频编码格式、采样率（Sample Rate）、声道架构（Channel Layout）完全相同。

#### **执行逻辑**

1. 内存中生成符合 FFmpeg `concat` 解复用器格式的虚拟文本列表，驱动 `AVInputFormat* concat_fmt = av_find_input_format("concat")`。  
2. 为支持空格、中文及日文 UTF-8 路径，采用内存缓冲区解析器 `avio_alloc_context` 将 safe 0 模式数据直接注入 `AVFormatContext`：

```
// 内存列表生成逻辑示例
std::string concat_manifest;
for (const auto& seg : inputs) {
    // 换行与转义处理，防止路径异常
    std::string escaped_path = sanitize_utf8_path(seg.file_path);
    concat_manifest += "file '" + escaped_path + "'\n";
    if (seg.in_point_us > 0) {
        concat_manifest += "inpoint " + std::to_string(seg.in_point_us / 1000000.0) + "\n";
    }
    if (seg.out_point_us > 0) {
        concat_manifest += "outpoint " + std::to_string(seg.out_point_us / 1000000.0) + "\n";
    }
}
```

3. 解复用器读取 Packet 后直接调用 `av_interleaved_write_frame` 写入目标 `AVFormatContext`（开启 `-movflags +faststart`），跳过任何 Decode/Encode 过程，实现 GB/s 级别的直拼导出。

### **2.3 异构片段 GPU 统一规格化无缝拼接（零磁盘中转）**

当 `check_homogeneous_fastpath` 返回 `false` 时，系统自动切入 GPU 规格化无缝拼接引擎，保证转码过程无磁盘中间文件。

```
                    [ 异构片段 GPU 规格化拼接流水线 ]

 片段 1 (1080p 24fps) ---> NVDEC ---> AI Enhancer -\
 片段 2 (720p 竖屏)   ---> NVDEC ---> AI Enhancer --+-> GPU Canvas Rescale -> NVENC -> MP4 Muxer
 片段 3 (4K 60fps)    ---> NVDEC ---> AI Enhancer -/     (SAR 1:1, 统一 PTS)   (+faststart)
```

#### **规格统一算法 (CanonicalOutputSpec)**

由 `EncoderJob.profile` 决定全局唯一的 `CanonicalOutputSpec`：

* **视频**：目标分辨率 \$(W\_{target}, H\_{target})\$、SAR \$1:1\$、目标帧率 \$FPS\_{target}\$（使用有理数表达，如 \$\\frac{60000}{1001}\$）、色彩空间（BT.709 或 BT.2020 PQ）。  
* **音频**：统一重采样为 `48000 Hz`、立体声 `AV_CH_LAYOUT_STEREO`、`AV_SAMPLE_FMT_FLTP`（AAC 编码）。

#### **视频流拼接与 GPU 规格化**

1. **跨片段边界防污染**：当前片段切换至下一片段时，硬件解码器 `NVDEC` 进行 `Flush` 操作；同时强行调用 `SceneCutGuard::reset()`，清空 RIFE / VSR 的前向历史帧缓存，防止跨片段交界出现融混畸变。  
2. **GPU Canvas 适配**：将解码后（及 AI 增强后）的 Surface 传递至 CUDA Kernel：  
   * 按 Keep-Aspect-Ratio 保持宽高比计算缩放，计算居中偏移 \$(offset\_x, offset\_y)\$。  
   * 在 GPU 显存内将背景区域填充为 YUV/RGB 纯黑（Blackbar Padding），输出统一 \$(W\_{target}, H\_{target})\$ 帧。  
3. **PTS 连续累加器**：  
   * 维护全局视频 PTS 计数器 `int64_t global_video_pts = 0`。  
   * 每段视频解码后，其相对 PTS 乘以目标时间基换算为全局增量。当前片段结束时，记录最后输出帧的下一个 PTS 预测值，并作为下一个片段的 `pts_offset`，确保送入 NVENC 的 Packet PTS 严格单调递增。

#### **音频流对齐与无音轨补齐**

1. **无音轨片段静音填充**：若某个片段不存在音频流，音频引擎根据该片段实际解码视频的精确总时长 \$T\_{sec}\$，生成 Sample Rate \= 48000 Hz 的全 0（Silence）PCM 帧，充当该片段的音频数据。  
2. **音视频尾差修齐**：每段结束时，比较该段产生的视频总时长 \$T\_v\$ 与音频总时长 \$T\_a\$。若 \$\|T\_v \- T\_a\| \> 0\$，采用 `swresample` 在音频末尾自动补齐静音样本或微调裁剪，确保每段结束时音视频时间戳绝对对齐。  
   \|---\|Encoder/Muxer 贯穿\*\*：后台工作线程在整个 `MergeConcat` 任务期间**仅初始化一次** `NVENCEncoder` 与 `FFmpeg MP4 Muxer`。所有片段的图像与音频流经规格化后，持续喂入同一 Muxer 实例，最终落盘为单一 `+faststart` `.mp4` 文件。

---

## **3\. 靶向 Task 2：`MediaPlaybackSession` 60fps AI 增强与 WASAPI 锁相**

### **3.1 消除悬空接口与视口绑定**

彻底移除 `workstation_canvas.h:79-88` 中的悬空 `active_vsr_target()` 存根。在 `MediaPlaybackSession` 与 `WorkstationCanvas` 之间建立无锁原子属性槽位：

```
// 替换 workstation_canvas.h 中的悬空存根
class WorkstationCanvas {
public:
    // 视口与 A/B 分屏状态原子更新接口
    void set_viewport_render_params(uint32_t width, uint32_t height, float split_ratio, bool split_enabled) noexcept {
        viewport_width_.store(width, std::memory_order_relaxed);
        viewport_height_.store(height, std::memory_order_relaxed);
        split_ratio_.store(split_ratio, std::memory_order_relaxed);
        split_enabled_.store(split_enabled, std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> viewport_width_{1920};
    std::atomic<uint32_t> viewport_height_{1080};
    std::atomic<float> split_ratio_{0.5f};
    std::atomic<bool> split_enabled_{false};
};
```

### **3.2 60fps AI 实时增强渲染管线**

播放器渲染主循环采用拓扑图所示的零拷贝 CUDA/D3D12 共享纹理管线：

```
 [NVDEC / SW-Upload]
         │
         ▼
 ┌─────────────────┐     Scene Cut?
 │ SceneCutGuard   │ ───────────────► [Clear RIFE History]
 └────────┬────────┘
          │
          ▼
 ┌─────────────────┐     Target Rate = 59.94 / 60 fps
 │  FRUC (RIFE)    │ ───────────────► [Rational Timebase Interpolation]
 └────────┬────────┘
          │
          ▼
 ┌─────────────────┐     Viewport Aware Downscale / Upscale
 │  VSR (TensorRT) │ ───────────────► [Scale to Viewport Width/Height]
 └────────┬────────┘
          │
          ▼
 ┌─────────────────┐
 │ TrueHDR / 3D LUT│ ───────────────► [PQ/HLG Tone-Mapping & Rec.709 Color]
 └────────┬────────┘
          │
          ▼
 ┌─────────────────┐     Split-Screen Interactive Mode
 │ A/B Split Render│ ───────────────► [Draw 2px Vertical Split Cursor]
 └────────┬────────┘
          │
          ▼
 ┌─────────────────┐
 │ D3D12Presenter  │ ───────────────► [DirectX 12 SwapChain Display]
 └─────────────────┘
```

#### **各节点执行规范**

1. **FRUC (RIFE 60fps 实时插帧)**：  
   * 以有理数分数节拍（如 NTSC \$\\frac{60000}{1001} \\approx 59.94\$ fps 或 \$60.00\$ fps）驱动插帧生成。  
   * 当源视频为 23.976 fps 时，计算生成精确插值帧 PTS；若 `SceneCutGuard` 检测到场景切换（SAD/光流突变），该插帧周期降级为重复前帧，防止画面撕裂。  
2. **VSR (视口感知代理降载)**：  
   * 读取 `viewport_width_` 与 `viewport_height_`。若当前画布被用户缩小（例如仅占据界面 \$960 \\times 540\$ 区域），VSR 不执行全分辨率 \$4K\$ 推理，自动降级为视口目标尺寸的 TensorRT 代理模型推理，降低 GPU 负载。  
3. **TrueHDR & 3D LUT**：  
   * 在 CUDA Color Kernel 中完成 PQ/HLG 转 SDR（Tone-Mapping）以及 3D LUT 色彩校准，输出 DXGI\_FORMAT\_R8G8B8A8\_UNORM 纹理。  
4. **A/B Split Split-Screen 渲染**：  
   * 当 `split_enabled_` 为 `true` 时，在 CUDA 端依据 `split_ratio_` 混合原帧与 AI 增强帧。  
   * 在分屏分割线上绘制 \$2\\text{px}\$ 宽度、RGBA 为 `(0xFF, 0xFF, 0xFF, 0xE0)` 的准星指示线。

### **3.3 WASAPI 锁相与死线熔断机制**

播放过程以 `WasapiAudioOutput` 设备的硬件 DMA 播放位置为主时钟（Master Clock \$T\_{audio}\$）。

```
// 渲染主循环中音视频同步与 AI 旁路熔断算法
void MediaPlaybackSession::render_video_frame() {
    double t_audio = audio_output_->get_hardware_audio_pts();
    double t_video_next = peek_next_frame_pts();
    
    // 计算视频相对于音频的超前/滞后时间
    double delta_ms = (t_video_next - t_audio) * 1000.0;
    
    // 超前死线判决：若 AI 推理超时导致滞后超过 8.0ms，触发单帧 AI 旁路熔断
    if (delta_ms < -8.0) {
        // [AI 旁路熔断] 跳过 RIFE 与 VSR 推理，直接将最近的 NVDEC 原始帧送入 D3D12Presenter 上屏
        presenter_->present_raw_frame(get_latest_decoded_frame());
        metrics_.ai_bypass_count.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (delta_ms > 16.67) {
        // 视频超前，等待下一个 V-Sync 节拍
        return;
    }

    // 正常时间窗口，执行完整 AI 渲染管线
    execute_ai_pipeline_and_present();
}
```

#### **状态切换与时域清空**

在用户触发 `Seek(target_pts)`、`StepFrame(+/-1)`、`SetSpeed(rate)` 时，系统必须在同一个原子操作中执行以下清空指令：

1. **预读环清空**：`AudioRingBuffer::clear()` 与 `VideoFrameQueue::clear()`。  
2. **WASAPI 硬件 Flush**：调用 `IXAudio2SourceVoice::FlushSourceBuffers` 或 WASAPI `IAudioClient::Stop` / `Reset`，杜绝爆音。  
3. **AI 时域上下文重置**：调用 `SceneCutGuard::reset()`，清空 RIFE 历史 Frame 队列与 TRT 隐藏状态，杜绝历史残影。

---

## **4\. 样式与数据结构定义**

---

## **5\. 宿主机验收门禁与测试用例**

为通过 Wave 3-A 最终验收，必须在 RTX 宿主机环境执行以下两组硬核门禁测试：

| 测试项 ID | 测试场景与输入条件 | 执行路径与核验指标 | 判定合格标准 |
| :---- | :---- | :---- | :---- |
| **TC-W3A-01** | **异构三段混拼转码**Seg1: `1080p@23.976 H.264`Seg2: `720x1280@30fps 竖屏无音轨`Seg3: `2560x1080@60fps FLAC` | 启用 `MergeConcat` 模式，勾选 RIFE (24-\>60fps) \+ VSR。查看磁盘 I/O 及 FFmpeg 流水线。 | 1\. 全程**零磁盘中转临时文件**。2. 导出单项 `.mp4` 文件（`moov` 头前置）。3. 音视频漂移 **`A/V Drift < 20 ms`**。 |
| **TC-W3A-02** | **60fps 实时增强播放与锁相测试**包含 23.976fps 电影片源，开启 RIFE \+ VSR \+ HDR 增强播放 60 秒。 | 监控 `WasapiAudioOutput` 时钟与视频帧上屏时间差。连续触发 50 次 `Seek` / `Step` / `Speed` 变换。 | 1\. 60秒连续播放 **`A/V Drift <= ±16.67 ms`**。2. 50 次操作过程**零死锁、零爆音、零残影**。3. 超级拖拽时 AI 旁路熔断机制正常触发并计数。 |

### **验收执行指令集**

```
# 1. 编译全工程（确保无编译警告与新增结构体校验）
cmake --build build --config Release

# 2. 运行异构拼接与转码门禁
.\build\bin\Release\host_test_suite.exe --gtest_filter=Wave3A_Transcode.HeterogeneousMergeConcat

# 3. 运行 60fps 播放器 WASAPI 锁相与 Seek 压力测试
.\build\bin\Release\host_test_suite.exe --gtest_filter=Wave3A_Playback.WASAPILockStep60FPS
```

