# **Wave 3-B 施工单：异构多任务编码队列与三路无缝合并引擎**

## **1\. 元信息与核心目标**

### **1.1 施工波次与定位**

本施工单属于 **Wave 3 \- Stage B**，与 Wave 3-A 并行推进。核心任务为彻底重构 `EncoderJobQueue` 及其底层数据流与调度架构，解除 GUI 检查器与后台作业队列的全局参数暴力覆盖耦合，并引入异构片段无中间文件无缝拼接引擎。

### **1.2 宪法红线规约 (Strict Rules)**

* **具名类型配额控制**：全工程具名类型总数严格维持 `AuditTypes <= 34/35`。**禁止**在头文件中新增任何具名 `struct`、`class` 或全局 `enum` 类型。所有升级必须在既有 `include/rtx/job_queue.h` 内的 `EncoderJob` 结构体、`EncoderJobQueue` 类及其自由函数中完成原位重构。  
* **容器封装规格**：所有输出文件封装格式强行固定为 `.mp4`，且必须在 Muxer 初始化时应用 `+faststart` 标记（移动 `moov` 原子至文件头以支持流式秒开）。

### **1.3 核心目标矩阵**

| 目标维度 | 现有代码缺陷 (As-Is) | 目标架构 (To-Be) |
| :---- | :---- | :---- |
| **队列参数隔离** | `src/gui/rtx_gui.cpp:312-323` 点击"开始转码"时遍历所有 `Pending` 任务，用当前 UI 检查器参数覆盖所有任务。 | 采用「自包含作业包 \+ 选中即编辑（Select-to-Inspect）」模式，每个 `EncoderJob` 独立持有 `TranscodeProfile`。 |
| **片段导出模式** | 仅支持单任务多文件独立导出 `_remaster_<codec>.mp4`。 | 增加 `JobOutputMode` 模式（独立导出 vs 无缝拼接），支持异构片段拼接导出为单一 `.mp4`。 |
| **合并执行效率** | 缺乏异构拼接支持，传统方式需落地中间临时文件。 | 实现三路路由引擎，异构路径基于统一 GPU Canvas 动态黑边填充与帧/采样率对齐，**零中间文件**持续写入同一编码器。 |
| **音画同步控制** | 多段拼接易产生音画累积漂移（Drift \> 100ms）。 | 引入 `int64_t` 有理数时间基与 NTSC 1001 因子扩展，全局音画漂移强行压制在 `A/V Drift < 20ms`。 |

---

## **2\. 问题代码坐标与缺陷诊断**

```
[src/gui/rtx_gui.cpp]
 312:  for (auto& job : queue.jobs()) {
 313:      if (job.status == JobStatus::Pending) {
 314:          // 灾难性缺陷：读取当前右侧检查器 UI 状态，强制覆盖整条队列中所有待处理任务
 315:          job.profile = current_inspector_profile; 
 316:          job.output_dir = current_inspector_output_dir;
 317:      }
 318:  }
```

### **缺陷 1：全局参数暴力覆写 (Global Parameter Override)**

在 `src/gui/rtx_gui.cpp:312-323` 中，用户为 Task A 设置了专属算子（如 RIFE 4.13 \+ VSR 2x），但在点击"开始"前选中 Task B 并调整参数，会导致 Task A 的配置被 Task B 强制覆写。队列失去了异构配置隔离能力。

### **缺陷 2：任务模型维度缺失 (Job Model Limitations)**

`include/rtx/job_queue.h` 中的 `EncoderJob` 仅包含 `std::vector<std::filesystem::path> inputs` 和单一 `TranscodeProfile profile`。缺乏针对单个任务内多文件的动态增删、上移/下移调序能力，且无法表达"多文件合并为一个导出文件"的语义。

### **缺陷 3：Win32 UI 交互局限 (Win32 ListBox Binding)**

Win32 Queue UI 当前使用普通的单行文本 `LISTBOX`。不支持单任务内子文件的展开与控制，也不支持在选中队列项时反向填充检查器控件（Select-to-Inspect），使得多任务参数校验极其脆弱。

---

## **3\. 详细执行步骤 (TDD Driven)**

```
                          [Win32 UI / WebUI / CLI]
                                     │
                        (Select-to-Inspect / Edit)
                                     ▼
                        ┌────────────────────────┐
                        │   EncoderJob (Pending) │
                        │  - inputs: path[]      │
                        │  - mode: OutputMode    │
                        │  - profile: Profile    │
                        └────────────┬───────────┘
                                     │ Thread Pick
                                     ▼
                        ┌────────────────────────┐
                        │ Immutable Job Snapshot │
                        │   + std::stop_token    │
                        └────────────┬───────────┘
                                     │
             ┌───────────────────────┼───────────────────────┐
             ▼                       ▼                       ▼
    [Path A: Direct Stream]  [Path B: GPU Seamless]  [Path C: Deep Transcode]
    - Homogeneous clips     - Heterogeneous clips   - Complex filter graph
    - Stream copy (fast)    - GPU canvas resize     - Dynamic Resampling
    - demuxer concat        - Zero intermediate     - Continuous PTS/DTS
             │                       │                       │
             └───────────────────────┼───────────────────────┘
                                     ▼
                         ┌───────────────────────┐
                         │ MP4 Muxer (+faststart)│
                         └───────────────────────┘
```

### **Task 1: 自包含作业包 `EncoderJob` 与状态隔离调度器重构**

#### **Step 1.1: 扩展 `include/rtx/job_queue.h` 既有结构体**

在不增加任何新具名类型的前提下，利用匿名联合体/枚举与内联成员扩展 `EncoderJob`。

```
// include/rtx/job_queue.h

#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <stop_token>
#include <cstdint>
#include "rtx/transcode_profile.h"

namespace rtx {

struct EncoderJob {
    enum class OutputMode : uint8_t {
        BatchSeparate = 0,  // 独立逐一导出：N 个输入 -> N 个 _remaster_<codec>.mp4
        MergeConcat   = 1   // 无缝拼接合并：N 个输入 -> 1 个指定 output_path .mp4
    };

    enum class Status : uint8_t {
        Pending = 0,
        Running,
        Completed,
        Failed,
        Canceled
    };

    uint64_t job_id{0};
    std::string job_name;
    
    // 输入文件队列 (动态可变：支持 Append / Remove / Swap)
    std::vector<std::filesystem::path> inputs;
    
    // 导出模式与专属配置
    OutputMode output_mode{OutputMode::BatchSeparate};
    TranscodeProfile profile;
    std::filesystem::path output_path; // 当 MergeConcat 时生效；BatchSeparate 时作为输出目录

    // 状态与执行控制
    Status status{Status::Pending};
    float progress{0.0f};
    std::string error_message;
    
    // 动态辅助 API（原位内联实现）
    bool move_input(size_t from_idx, size_t to_idx) {
        if (from_idx >= inputs.size() || to_idx >= inputs.size()) return false;
        if (from_idx == to_idx) return true;
        auto item = std::move(inputs[from_idx]);
        inputs.erase(inputs.begin() + from_idx);
        inputs.insert(inputs.begin() + to_idx, std::move(item));
        return true;
    }

    bool remove_input(size_t idx) {
        if (idx >= inputs.size()) return false;
        inputs.erase(inputs.begin() + idx);
        return !inputs.empty();
    }
};

// 任务快照（用于后台线程零竞态运行）
struct RunningJobSnapshot {
    EncoderJob job_data; // 完全深拷贝
    std::stop_source stop_source;
};

} // namespace rtx
```

#### **Step 1.2: 彻底废除 `rtx_gui.cpp` 全局覆写逻辑**

删除 `src/gui/rtx_gui.cpp:312-323` 中的遍历赋值逻辑，重构 UI 事件处理响应：

1. **Select-to-Inspect 联动协议**：  
   * 当用户点击 GUI 队列列表中的某一 `Pending` 任务时，触发 `OnQueueItemSelected(job_id)`。  
   * GUI 将该 `job.profile` 与 `job.output_mode` 加载至右侧 Inspector 界面。  
   * 用户在 Inspector 中的任何修改，实时写回 `job.profile`。  
   * 当未选中任何任务时，Inspector 编辑器修改的是全局 `DefaultTemplateProfile`。  
2. **零竞态线程调度 (Thread-Safe Dispatch)**：  
   * 工作线程（Worker Thread）从 `EncoderJobQueue` 弹出任务时，调用 `queue.pop_and_lock_running()`。  
   * 该函数将 `EncoderJob` 深拷贝至 `RunningJobSnapshot`，并将原队列项状态标记为 `Running`。  
   * 后台编码器仅读取 Snapshot 中的不可变数据。用户在 GUI 上对队列中其他 `Pending` 任务的编辑、删除或顺序调整，完全不会引发任何竞态条件。

---

### **Task 2: 异构多文件合并（Merge/Concat）三路智能执行引擎**

#### **Step 2.1: 时间基与 NTSC 时基因子扩展**

吸收原 `docs/TODO` 规约，采用 `AVRational` 及 `int64_t` 精确有理数计算。定义统一标准时间基 `STD_TIME_BASE = {1, 90000}` 及 `AUDIO_TIME_BASE = {1, 48000}`。对于 23.976fps / 29.97fps / 59.94fps 等 NTSC 帧率，使用精确分数表示（例如 `24000/1001`），严禁转换为 `float` 或 `double` 引入舍入误差。

#### **Step 2.2: 三路智能路由机制 (Three-Path Routing Engine)**

在任务启动前，执行器对 `inputs[0..N-1]` 依次执行 `avformat_open_input` 和 `avformat_find_stream_info` 收集元数据，计算并确定路由路径。

```
                       [探查 inputs[0..N-1] 元数据]
                                    │
           ┌────────────────────────┴────────────────────────┐
           │ 是否满足零重编直拼条件？                        │
           │ 1. 均未开启任何 AI / 色彩 / 缩放滤镜            │
           │ 2. Codec / Resolution / FPS / SAR /            │
           │    Format / SampleRate / Channels / tbn        │
           │    完全一致                                    │
           └───────────┬─────────────────────────┬───────────┘
                       │ 是                      │ 否
                       ▼                         ▼
             [路径 A: Direct Stream]        ┌────────────────────────┐
                                            │ 是否挂载 AI / 复杂滤镜 │
                                            └───┬────────────────┬───┘
                                                │ 否             │ 是
                                                ▼                ▼
                                      [路径 B: GPU Canvas] [路径 C: Complete Pipeline]
```

#### **Step 2.3: 路径 A——同构零重编极速直拼 (Direct Stream Copy Concat)**

* **条件**：所有输入片段的视频 Encoding Codec、Width/Height、SAR、Exact Framerate、Pixel Format、Audio SampleRate、Channels 及其 Stream `time_base` 完全相同，且 Profile 中**未启用任何 AI / 色彩 / 缩放滤镜**。  
* **实现逻辑**：  
  1. 在内存中动态生成 FFmpeg concat demuxer 格式配置字符串（使用 UTF-8 编码，安全处理中文、日文与空格路径）：

```
file 'C:/Videos/Clip 01.mp4'
file 'C:/Videos/Clip 02.mp4'
```

  2. 使用 `av_find_input_format("concat")` 动态加载解复用器，配置 `safe = 0`。  
  3. 直接使用 `av_interleaved_write_frame` 将 Packet 重新写入 target Muxer，跳过所有解码、滤镜及编码阶段。  
  4. 写入完成后应用 `+faststart` 移动 `moov` 头部。

#### **Step 2.4: 路径 B——异构片段 GPU 统一规格化无缝连续编码拼接 (核心主路径)**

当输入片段存在分辨率差异、横竖屏混拼（如 `16:9` 与 `9:16`）、帧率不同、或部分片段缺失音轨/声道不一致，或挂载了 AI 管线（RIFE/NVOFA/VSR/TrueHDR）时，启用路径 B。

##### **1\. 规格归一化 (Canonical Output Specification)**

根据 `inputs[0]` 或 Profile 中的显式要求，锁定全局唯一的输出规范：

* **Canvas Resolution**: \$W\_{out} \\times H\_{out}\$ (例如 \$1920 \\times 1080\$)  
* **SAR**: `1:1`  
* **Target FPS**: \$FPS\_{out}\$ (例如 `60/1` 或 `60000/1001`)  
* **Audio Format**: `48000 Hz`, `Stereo FLTP AAC`

##### **2\. Frame Processing & GPU Letterbox Alignment**

对于每个片段 \$i \\in \[0, N-1\]\$：

* **Scene Cut Guard 强置**：在切换到片段 \$i\$ 的首帧时，**强制触发 AI 模块（如 RIFE / TrueHDR）的 `reset_state()` / `SceneCutGuard`**。清空历史帧缓存，防止跨文件生成无效光流或色彩过渡畸变。  
* **Aspect Ratio & Letterboxing**：将解码后的异构画面输入 GPU CUDA/FFmpeg Filter Kernel，按照 `Scale-to-Fit` 原则计算黑边填充：  
  \$\$\\text{Scale Ratio } s \= \\min\\left(\\frac{W\_{out}}{W\_{in}}, \\frac{H\_{out}}{H\_{in}}\\right)\$\$  
  \$\$W\_{scaled} \= \\lfloor W\_{in} \\cdot s \\rfloor, \\quad H\_{scaled} \= \\lfloor H\_{in} \\cdot s \\rfloor\$\$  
  \$\$\\text{Pad Left } X \= \\lfloor (W\_{out} \- W\_{scaled}) / 2 \\rfloor, \\quad \\text{Pad Top } Y \= \\lfloor (H\_{out} \- H\_{scaled}) / 2 \\rfloor\$\$  
  在 GPU 内存中将图像居中渲染至 \$W\_{out} \\times H\_{out}\$ 的纯黑（YUV: `0, 128, 128` 或 RGB: `0,0,0`）画布上。

##### **3\. Continuous Audio Padding & Resampling**

* **缺失音轨补齐 (Silent PCM Generation)**：若片段 \$i\$ 无音频流，计算该片段精确视频时长 \$T\_i \= \\text{FrameCount}*i / FPS*{out}\$。按 \$48000 \\text{ Hz}\$ 立体声标准生成采样点数为 \$N\_{samples} \= \\lfloor T\_i \\times 48000 \\rfloor\$ 的 `0.0f` 静音 PCM 帧填入音频管线。  
* **音频重采样与声道映射**：使用 `swr_convert` 将任意 \$8\\text{kHz}-192\\text{kHz}\$、单声道/5.1 声道的音频统一重采样为 \$48000 \\text{ Hz}\$ 立体声。

##### **4\. PTS/DTS 连续累加与 Zero Intermediate Buffer**

定义全局时间偏移量 \$\\Delta T\_{video}\$ 与 \$\\Delta T\_{audio}\$（单位：系统统一时间基）。

```
// 帧时间戳无缝连续接续伪代码
int64_t global_video_pts_offset = 0;
int64_t global_audio_pts_offset = 0;

for (size_t i = 0; i < job.inputs.size(); ++i) {
    auto decoder = CreateDecoder(job.inputs[i]);
    int64_t last_clip_video_pts = 0;
    int64_t last_clip_audio_pts = 0;

    // 强制触发 AI 状态重置，防止跨视频插帧
    ai_pipeline->reset_scene_cut();

    while (decoder->read_frame(frame)) {
        if (frame.type == VIDEO) {
            AVFrame* processed_frame = ai_pipeline->process(frame);
            AVFrame* canvas_frame = gpu_letterbox_transform(processed_frame, W_out, H_out);
            
            // PTS 首尾连续平移
            canvas_frame->pts += global_video_pts_offset;
            last_clip_video_pts = canvas_frame->pts + canvas_frame->duration;

            encoder->send_frame(canvas_frame);
            muxer->write_encoded_packets();
        } else if (frame.type == AUDIO) {
            AVFrame* resampled_frame = audio_resampler->process(frame);
            
            resampled_frame->pts += global_audio_pts_offset;
            last_clip_audio_pts = resampled_frame->pts + resampled_frame->nb_samples;

            audio_encoder->send_frame(resampled_frame);
            muxer->write_encoded_packets();
        }
    }

    // 消除单段尾差：强行将音视频偏移量对齐至当前片段最大结束点
    int64_t max_clip_end = std::max(last_clip_video_pts, last_clip_audio_pts);
    global_video_pts_offset = max_clip_end;
    global_audio_pts_offset = max_clip_end;
}
```

全过程数据流直接由 Decoder \-\> Pipeline \-\> Encoder \-\> Muxer 内存管道透传，**零磁盘临时文件落盘**。

#### **Step 2.5: 路径 C——复杂滤镜图与极端异构重建 (Deep Pipeline Processing)**

当 Profile 配置了复杂的 AVFilterGraph 或音频多轨混音图时，系统自动构建统一的 `AVFilterGraph` 结构，将各片段作为输入节点连接至 `concat` 滤镜节点（`concat=n=N:v=1:a=1`），交由 FFmpeg 滤镜图引擎进行深度解码拼接。

---

### **Task 3: CLI 与 WebUI 队列接口对齐**

#### **Step 3.1: CLI 参数解析扩充**

在 `src/cli/main.cpp` 中引入 `--merge` 参数支持：

```
# 示例 1：异构多文件合并输出单个视频
rtx_cli --input "clip1.mp4" --input "clip2.mkv" --merge --output "final_seamless.mp4" --profile "RIFE_4K_60FPS.json"

# 示例 2：多文件批量独立导出
rtx_cli --input "clip1.mp4" --input "clip2.mkv" --output "./out_dir/" --profile "Fast_Convert.json"
```

#### **Step 3.2: WebUI JSON Schema 协议同步**

升级 WebUI API endpoints（`/api/jobs`），保证前端可准确序列化与反序列化异构任务：

```
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "EncoderJobSchema",
  "type": "object",
  "properties": {
    "job_id": { "type": "integer" },
    "job_name": { "type": "string" },
    "inputs": {
      "type": "array",
      "items": { "type": "string" },
      "minItems": 1
    },
    "output_mode": {
      "type": "string",
      "enum": ["BatchSeparate", "MergeConcat"]
    },
    "output_path": { "type": "string" },
    "profile": {
      "type": "object",
      "description": "TranscodeProfile 完整自包含对象"
    }
  },
  "required": ["job_id", "inputs", "output_mode", "profile"]
}
```

---

## **4\. 验收门禁 (Acceptance Criteria)**

### **Test Case 1: 极端异构三段无缝合并硬核测试**

* **测试输入源**：  
  1. `Clip A`: 1080p @ 23.976 fps, H.264, AAC Stereo, 16:9  
  2. `Clip B`: 720x1280 @ 30.000 fps (竖屏), HEVC, 无音轨  
  3. `Clip C`: 2560x1080 @ 60.000 fps (21:9 超宽屏), AV1, FLAC 5.1 Channel  
* **开启参数**：开启 RIFE (目标帧率 60fps) \+ VSR (目标 4K 画质)，设置 `output_mode = MergeConcat`。  
* **期望结果与指标**：  
  - [x] **无中间文件**：全程监控磁盘 I/O，严禁生成任何 `.tmp` / `temp_*.mp4` 临时文件。  
  - [x] **音画同步率**：对合并后的 `final.mp4` 进行全长音画分析，全片 `A/V Drift < 20 ms`。  
  - [x] **硬切防插帧验证**：检查 Clip A 与 Clip B 衔接处（边界帧），RIFE 必须触发 `SceneCutGuard`，不得产生跨视频的鬼影/融合插帧。  
  - [x] **画面黑边填充**：Clip B (竖屏) 在 4K 16:9 画布上左右正确填充 Black Pillarbox；Clip C (21:9) 上下正确填充 Black Letterbox。  
  - [x] **播放器流式秒开**：生成的 MP4 文件中 `moov` 原子位于文件头部，Chrome/VLC 能够秒开播放。

### **Test Case 2: 队列隔离与动态编辑压力测试**

* **步骤**：  
  1. 在队列中创建 Job 1 (`Pending`, Profile A: CRF 18\) 和 Job 2 (`Pending`, Profile B: CQ 28)。  
  2. 启动 Job 1（状态变更为 `Running`）。  
  3. 在 GUI 检查器中选中 Job 2，将其 Profile 修改为 Profile C (CRF 12)，并向 Job 2 的 inputs 列表中追加 2 个新文件，上移第 3 个文件至首位。  
* **期望结果**：  
  - [x] Job 1 执行不受任何干扰，其后台运行快照（Snapshot）参数与文件顺序保持初始状态。  
  - [x] Job 1 执行完毕后，Job 2 启动，严格按照修改后的 Profile C 及最新调整的文件顺序执行。  
  - [x] 全过程无 Crash、无内存泄漏、无多线程竞态断言触发。

