# **19\_rework\_wave3-B\_异构多任务编码队列与三路无缝合并实装施工单**

Superpowers 原子执行返工规约（依据协调负责人驳回裁决 CINERTX-P13-RULING-013 第 13 号文档）

## **1\. 元信息与驳回定性复盘**

### **1.1 施工元信息**

| 属性项 | 详细规格 / 状态说明 |
| :---- | :---- |
| **返工波次** | **Rework Wave 3-B**（与 Wave 3-A 并行推进：彻底填补 Wave 3-B"严重空壳"缺口） |
| **目标模块** | `include/rtx/job_queue.h`, `src/core/job_queue.cpp`, `src/gui/rtx_gui.cpp`, `src/io/ffmpeg_io.cpp` |
| **核心使命** | 落地任务内多文件动态增删调序、运行期快照隔离及三路异构合并拼接引擎 |
| **宪法红线** | `AuditTypes <= 34/35`（零新增具名 `struct`/`class`，必须基于既有结构扩展） |
| **输出封装** | 强制锁定 `.mp4` 格式并写入 `+faststart` 标记（支持流式加载） |

### **1.2 驳回定性复盘 (`CINERTX-P13-RULING-013`)**

原 Wave 3-B（09 号文档）提交的代码被裁决为**严重空壳（REJECTED）**。审查判定依据如下：

* **核心头文件与实现零变动**：`include/rtx/job_queue.h` 与 `src/core/job_queue.cpp` 在上一波次中**一行未改**。  
* **伪完成欺诈**：仅在 `src/gui/rtx_gui.cpp:338` 停止了覆盖 `Pending` 队列的临时逻辑，未提供底层数据结构与引擎支持。  
* **关键功能缺失**：  
  * **动态片段管理缺失**：无法在单个任务中进行片段的动态增加、删除与上下移动调序。  
  * **拼接引擎缺失**：完全缺失"独立逐一导出 vs 无缝合并为单一 `.mp4`（Merge/Concat）"的三路智能执行引擎。  
  * **并发安全隐患**：前台 GUI 界面参数调节与后台转码线程未做运行期快照隔离，存在竞态破坏。

---

## **2\. Task 1：升级作业队列与快照隔离架构**

### **2.1 精确修改坐标**

* `include/rtx/job_queue.h`  
* `src/core/job_queue.cpp`  
* `src/gui/rtx_gui.cpp:338`

### **2.2 扩展既有数据结构（严格守恒红线：零新增类型名）**

在 `include/rtx/job_queue.h` 既有 `EncoderJob` 结构体中直接扩展字段，严禁声明新的 `struct` 或 `class`。

```
// include/rtx/job_queue.h

enum class JobOutputMode : uint8_t {
    BatchSeparate = 0, // 独立逐一导出多个 MP4 文件
    MergeConcat   = 1  // 无缝合并为单一 MP4 文件
};

struct EncoderJob {
    uint64_t id{0};
    std::string name;
    JobStatus status{JobStatus::Pending};
    
    // Wave 3-B 扩展字段（直接嵌入，不扩充类型总数）
    std::vector<std::filesystem::path> inputs; // 多片段文件列表（支持动态调序）
    JobOutputMode output_mode{JobOutputMode::BatchSeparate};
    TranscodeProfile profile;                  // 每任务独立的完整编码/AI参数
    std::filesystem::path output_path;        // 合并文件路径或输出目录/模板
    
    float progress{0.0f};
    std::stop_source stop_source;              // 专属取消控制句柄
};
```

### **2.3 线程安全操控 API 与运行期快照隔离**

在 `EncoderJobQueue` 中实现严格仅针对 `JobStatus::Pending` 任务的可变编辑接口。工作线程拾取任务时，必须生成不可变运行快照。

```
// src/core/job_queue.cpp

bool EncoderJobQueue::addClipToJob(uint64_t job_id, const std::filesystem::path& clip_path) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    auto it = findJobByIdUnlocked(job_id);
    if (it != jobs_.end() && it->status == JobStatus::Pending) {
        it->inputs.push_back(clip_path);
        return true;
    }
    return false; // 非 Pending 状态禁止修改
}

bool EncoderJobQueue::moveClipInJob(uint64_t job_id, size_t from_idx, size_t to_idx) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    auto it = findJobByIdUnlocked(job_id);
    if (it != jobs_.end() && it->status == JobStatus::Pending) {
        if (from_idx < it->inputs.size() && to_idx < it->inputs.size()) {
            auto item = it->inputs[from_idx];
            it->inputs.erase(it->inputs.begin() + from_idx);
            it->inputs.insert(it->inputs.begin() + to_idx, item);
            return true;
        }
    }
    return false;
}

// 线程拾取与运行期快照隔离
std::optional<EncoderJobSnapshot> EncoderJobQueue::popNextJobForExecution() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (auto& job : jobs_) {
        if (job.status == JobStatus::Pending) {
            job.status = JobStatus::Running;
            // 产生深拷贝快照，与前台 UI 编辑完全隔离
            EncoderJobSnapshot snapshot{
                .job_id = job.id,
                .inputs = job.inputs,
                .output_mode = job.output_mode,
                .profile = job.profile, // 完整深拷贝 TranscodeProfile
                .output_path = job.output_path,
                .stop_token = job.stop_source.get_token()
            };
            return snapshot;
        }
    }
    return std::nullopt;
}
```

### **2.4 GUI「选中即编辑」闭环绑案**

修改 `src/gui/rtx_gui.cpp:338`，实现 Inspect 界面与队列选中项的双向数据绑定：

```
// src/gui/rtx_gui.cpp:338
void RenderJobInspector() {
    int selected_idx = GetSelectedJobIndex();
    if (selected_idx < 0 || selected_idx >= g_JobQueue.size()) return;
    
    EncoderJob& job = g_JobQueue.getJobMutRef(selected_idx);
    
    // 状态防护：运行中或已完成的任务锁定编辑UI
    bool is_editable = (job.status == JobStatus::Pending);
    ImGui::BeginDisabled(!is_editable);
    
    // 1. 片段增删与拖拽调序列表
    RenderClipListViewer(job.inputs);
    
    // 2. 导出模式切换 (Batch vs Merge)
    int mode = static_cast<int>(job.output_mode);
    if (ImGui::RadioButton("独立逐一导出", &mode, 0)) job.output_mode = JobOutputMode::BatchSeparate;
    ImGui::SameLine();
    if (ImGui::RadioButton("合并为单一 MP4", &mode, 1)) job.output_mode = JobOutputMode::MergeConcat;
    
    // 3. 独立 Profile 参数调节（实时同步至该 Task，绝不污染全局或其他任务）
    RenderProfileInspector(job.profile);
    
    ImGui::EndDisabled();
}
```

---

## **3\. Task 2：实装三路智能合并执行引擎**

引擎根据 `inputs` 规格、挂载滤镜及 `output_mode` 自动选择最优执行路径。

```
                       ┌──────────────────────────┐
                       │   EncoderJobSnapshot     │
                       └────────────┬─────────────┘
                                    │
                       ┌────────────┴─────────────┐
                       │ Output Mode == Merge?    │
                       └──────┬─────────────┬─────┘
                              │ No          │ Yes
                              ▼             │
             ┌──────────────────────────┐   │
             │ Path 1: Batch Separate   │   │
             │ (逐一独立导出 MP4)       │   │
             └──────────────────────────┘   │
                                            ▼
                                ┌───────────────────────┐
                                │ 无 AI 滤镜 & 编码参数/│
                                │ 容器时间基完全一致?   │
                                └───┬───────────────┬───┘
                                    │ Yes           │ No
                                    ▼               ▼
                       ┌────────────────┐ ┌─────────────────────────┐
                       │ Path 2: Concat │ │ Path 3: Heterogeneous   │
                       │ Demuxer 直拼   │ │ Unified Pipeline        │
                       │ (零重编码)     │ │ (GPU 规格化连续编码)    │
                       └────────────────┘ └─────────────────────────┘
```

### **3.1 Path 1：`BatchSeparate` 独立逐一导出**

当 `output_mode == JobOutputMode::BatchSeparate` 时：

1. 遍历 `inputs[0..N-1]` 列表；  
2. 针对每个片段，以 `profile` 为配置独立构建解码、AI 处理与编码管线；  
3. 输出文件名自动格式化为 `<output_dir>/<job_id>_<idx>_<original_name>.mp4`；  
4. 写入 `+faststart` 标记。

### **3.2 Path 2：`MergeConcat` 同构零重编极速直拼**

**触发条件**：

* `output_mode == JobOutputMode::MergeConcat`  
* Profile 中未开启任何 AI 增强（RIFE/NVOFA/VSR/TrueHDR 均为 false）、无色彩/缩放滤镜  
* 探针检测（`avformat_open_input` \+ `avformat_find_stream_info`）确认所有 input 片段：  
  * 视频编码 ID (Codec ID)、Width、Height、SAR、FR (`r_frame_rate`)、Pixel Format 完全完全相同  
  * 音频 Codec ID、Sample Rate、Channels/Channel Layout 完全相同  
  * 时间基 (`time_base.den / time_base.num`) 完全一致

**执行逻辑**：  
采用安全的内存文件列表驱动 FFmpeg `concat` 解复用器（等效 `-safe 0`），使用流复制 (`copy`) 拼接，输出 `+faststart` MP4：

```
// src/io/ffmpeg_io.cpp (Path 2 Concat Demuxer)
bool ExecuteDemuxerConcat(const std::vector<std::filesystem::path>& inputs, const std::filesystem::path& output_path) {
    // 使用内存自定义 AVIOContext 或生成加密临时 filelist 格式
    std::string concat_manifest = "";
    for (const auto& path : inputs) {
        // 安全转义 UTF-8 中文/日文及空格路径
        std::string escaped_path = EscapePathForConcat(path.u8string());
        concat_manifest += "file '" + escaped_path + "'\n";
    }
    
    // 配置 demuxer 参数，使用 copy 模式零 CPU/GPU 重编码开销
    return RunFFmpegConcatCopyEngine(concat_manifest, output_path, "movflags=+faststart");
}
```

### **3.3 Path 3：`MergeConcat` 异构 GPU 统一规格化连续编码拼接 (零磁盘中间大文件)**

当片段存在规格差异（分辨率、横竖屏、帧率、编码格式不同）或挂载了 AI 管线时，启动 Paths 3 引擎。**严禁生成任何临时中间文件！**

#### **步骤 1：确立唯一 `CanonicalOutputSpec`**

根据 `profile` 或首个片段规格计算基准度量：

* 目标宽高：`profile.target_width x profile.target_height`（若未指定，取首个片段分辨率）  
* Target SAR: `1:1`  
* Target FPS: `profile.target_fps`（如 60.0 fps）  
* Target Audio: `48000 Hz`, `Stereo (2 channels)`, `AAC` 编码

#### **步骤 2：逐片段 GPU 规格化与 AI 帧边界硬隔离**

针对每个输入的片段 \$k \\in \[0, N-1\]\$：

1. **场景切换硬隔离 (Scene Cut Guard)**：在切换至下一个片段时，强制调用 `ai_pipeline->ResetState()`。**严禁跨片段边界进行 RIFE 帧插值或 Temporal VSR 历史帧缓存积累！**  
2. **GPU 等比缩放与 Letterbox/Pillarbox 画布对齐**：  
   在 CUDA/Vulkan 显存中，按原图宽高比计算缩放尺寸，居中绘制至 `CanonicalOutputSpec` 尺寸的画布上，四周填充 `RGB(0,0,0)` 黑边。

\$\$\\text{Scale} \= \\min\\left(\\frac{W\_{\\text{target}}}{W\_{\\text{in}}}, \\frac{H\_{\\text{target}}}{H\_{\\text{in}}}\\right)\$\$

\$\$W\_{\\text{draw}} \= W\_{\\text{in}} \\times \\text{Scale}, \\quad H\_{\\text{draw}} \= H\_{\\text{in}} \\times \\text{Scale}\$\$

\$\$\\text{OffsetX} \= \\frac{W\_{\\text{target}} \- W\_{\\text{draw}}}{2}, \\quad \\text{OffsetY} \= \\frac{H\_{\\text{target}} \- H\_{\\text{draw}}}{2}\$\$

#### **步骤 3：静音补齐、重采样与无缝 PTS 连续平移**

```
// src/core/job_queue.cpp (Path 3 核心连续送编码逻辑)

struct MergeStreamContext {
    int64_t last_video_pts{0};
    int64_t last_audio_pts{0};
    uint64_t total_video_frames{0};
    AVCodecContext* encoder_ctx{nullptr};
    AVFormatContext* muxer_ctx{nullptr};
};

void ProcessHeterogeneousMergeJob(const EncoderJobSnapshot& job) {
    MergeStreamContext ctx;
    InitUnifiedEncoderAndMuxer(job, &ctx); // 全局仅创建一次 Encoder 与 Muxer
    
    for (size_t clip_idx = 0; clip_idx < job.inputs.size(); ++clip_idx) {
        if (job.stop_token.stop_requested()) break;
        
        auto decoder = CreateDecoder(job.inputs[clip_idx]);
        bool has_audio = decoder->HasAudioTrack();
        
        // 重置 AI 管线边界，防止跨片段误插帧
        g_AIPipeline.ResetSceneState();
        
        int64_t segment_start_v_pts = ctx.last_video_pts;
        int64_t segment_start_a_pts = ctx.last_audio_pts;
        int64_t segment_v_frame_count = 0;
        
        while (auto frame = decoder->ReadNextVideoFrame()) {
            // 1. GPU 画布规格化 (Letterbox/Pillarbox)
            GPUFrame norm_frame = NormalizeToCanvas(frame, job.profile.target_w, job.profile.target_h);
            
            // 2. AI 处理链 (RIFE / VSR)
            auto processed_frames = g_AIPipeline.Process(norm_frame);
            
            // 3. 平移 PTS 并持续压入同一个 Encoder
            for (auto& out_frame : processed_frames) {
                out_frame.pts = segment_start_v_pts + segment_v_frame_count * VIDEO_TIMEBASE_STEP;
                EncodeAndWriteVideoFrame(ctx, out_frame);
                segment_v_frame_count++;
            }
        }
        
        // 音频处理：重采样 / 静音补齐
        double segment_duration_sec = (double)segment_v_frame_count / job.profile.target_fps;
        
        if (!has_audio) {
            // 无音轨片段：生成精准匹配视频时长的 0.0f 静音 PCM 缓冲
            GenerateAndEncodeSilencePCM(ctx, segment_start_a_pts, segment_duration_sec);
        } else {
            // 有音轨：重采样至 48kHz 立体声，平移 PTS 送入同一个 Audio Encoder
            ProcessAndAlignAudioTrack(ctx, decoder.get(), segment_start_a_pts, segment_duration_sec);
        }
        
        // 更新累加 PTS 基准
        ctx.last_video_pts = segment_start_v_pts + segment_v_frame_count * VIDEO_TIMEBASE_STEP;
        ctx.last_audio_pts = segment_start_a_pts + (int64_t)(segment_duration_sec * 48000);
    }
    
    FlushEncoderAndFinalizeMuxer(ctx); // 刷新编码器，写入 MP4 +faststart 头部
}
```

---

## **4\. Task 3：对齐 CLI 与 WebUI 接口**

### **4.1 CLI `--merge` 命令行参数支持**

升级 CLI 解析器，增加 `--merge` 标志及多 `-i` 输入解析：

```
# 命令行合并示例（将多个异构输入合并为一个 MP4）
./cinertx_cli -i clip1.mp4 -i clip2.mkv -i clip3.webm --merge -o output_merged.mp4 --profile vsr_rife_4k60.json
```

### **4.2 WebUI `/api/jobs` RESTful API JSON Schema**

升级 JSON 交互结构，全面支持单任务多 inputs 及 `output_mode` 配置：n  
{  
"\$schema": "https\://json-schema.org/draft/2020-12/schema",  
"title": "EncoderJobRequest",  
"type": "object",  
"properties": {  
"job\_name": { "type": "string" },  
"inputs": {  
"type": "array",  
"items": { "type": "string" },  
"minItems": 1  
},  
"output\_mode": {  
"type": "string",  
"enum": \["BatchSeparate", "MergeConcat"\]  
},  
"output\_path": { "type": "string" },  
"profile": {  
"type": "object",  
"properties": {  
"encoder": { "type": "string", "enum": \["hevc\_nvenc", "h264\_nvenc", "av1\_nvenc"\] },  
"crf": { "type": "integer", "minimum": 0, "maximum": 51 },  
"target\_width": { "type": "integer" },  
"target\_height": { "type": "integer" },  
"enable\_rife": { "type": "boolean" },  
"enable\_vsr": { "type": "boolean" }  
},  
"required": \["encoder", "crf"\]  
}  
},  
"required": \["inputs", "output\_mode", "output\_path", "profile"\]  
}

```

---

## 5. 法定验收门禁 (真机实测与单元测试)

### 5.1 宿主机真机异构混拼测试场景

必须在目标硬件上运行以下混拼测试，校验音视频同步与无中间文件逻辑：

#### 测试输入源组合
```

\[Input 1\] 1920x1080 @ 23.976 fps \| H.264 \| AAC 立体声 (带音轨)  
\[Input 2\]  720x1280 @ 30.000 fps \| HEVC  \| 竖屏 (无音轨)  
\[Input 3\] 2560x1080 @ 60.000 fps \| AV1   \| FLAC 5.1声道 (带音轨)

````

#### 执行命令与配置

```bash
./cinertx_cli \
  -i test_inputs/1080p_23976.mp4 \
  -i test_inputs/720x1280_30_noaudio.mp4 \
  -i test_inputs/219_av1_flac.mkv \
  --merge \
  --enable-rife --target-fps 60 \
  --enable-vsr --target-res 3840x2160 \
  -o test_outputs/final_hetero_merged.mp4
````

### **判定通过指标**

```
[PASS] 零磁盘中间文件：监控临时目录，转码全程无 .tmp / .raw 大文件生成。
[PASS] 音视频同步：全片播放检查，全局 A/V Drift < 20 ms。
[PASS] 画面绘制：Input 2 自动填充两侧黑边 (Pillarbox)，Input 3 自动填充上下黑边 (Letterbox)。
[PASS] 边界隔离：Input 1 到 Input 2 切换处，RIFE 未出现跨片段图像拉伸/重影。
[PASS] 静音补齐：Input 2 播放段落声音平稳为无声 PCM，无爆音或卡顿。
[PASS] 容器标准：ffprobe final_hetero_merged.mp4 确认为标准 H.264/HEVC + AAC 格式，包含 +faststart 头部。
```

## **5.2 队列并发与单元测试集**

执行单元测试，保证修改未破坏队列稳定性：

```
ctest --output-on-failure -R test_job_queue
```

```
Running tests...
Test project /build
    Start 1: test_job_queue_clip_mutation
1/4 Test #1: test_job_queue_clip_mutation ......   Passed    0.02 sec
    Start 2: test_job_queue_snapshot_isolation ...   Passed    0.05 sec
2/4 Test #2: test_job_queue_snapshot_isolation ...   Passed    0.05 sec
    Start 3: test_job_queue_concurrent_edits .....   Passed    0.12 sec
3/4 Test #3: test_job_queue_concurrent_edits .....   Passed    0.12 sec
    Start 4: test_job_queue_hetero_merge .........   Passed    1.45 sec
4/4 Test #4: test_job_queue_hetero_merge .........   Passed    1.45 sec

100% tests passed, 0 tests failed out of 4.
```

