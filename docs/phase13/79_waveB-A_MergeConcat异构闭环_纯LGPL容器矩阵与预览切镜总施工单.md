# **79\_waveB-A\_MergeConcat异构闭环\_纯LGPL容器矩阵与预览切镜总施工单**

本文档为 Superpowers 原子施工规约，严格依据 76 号终审裁决《CineRTX Phase 13 74号返工终审验收报告与单EXE极限Modern C++重构演进方案》制定。

## **1\. 施工元数据与分工定调**

### **1.1 施工波次定位**

本施工单属于 Wave B \- Stage A，必须在 `77_wave0-A` 与 `78_wave0-B` 彻底闭环验收后执行。

重点目标包括：

* `MergeConcat` 异构路由最小增量复现与管道贯通。  
* SVT-AV1 10-bit CRF 实片转码与固定 `.mp4` 预裁决兼容矩阵。  
* 基于标准库 `<chrono>` 的 `1001` 时基全体系精确定向。  
* 独立任务队列参数隔离。  
* 预览槽位代数同步与公共切镜黑场总线机制。

### **1.2 协同分工定调（依据 76 号裁决批复 3）**

`areConcatInputsHomogeneous()` 与 `MergeConcat` 异构路由至 `runNvidiaRemasterRenderGraph()` 及其单元测试，必须以\*\*最小函数级上下文补丁（Unified Diff）\*\*增量合入，严格禁止基于旧快照进行整文件覆写。

### **1.3 宪法硬红线**

1. **零新增自定义类**：  
   * NTSC `1001` 时基直接采用标准库 `<chrono>` 编译期有理数时钟（如 `std::chrono::duration<int64_t, std::ratio<1001, 24000>>`），严禁自造 `Rational` / `Fraction` 类。  
   * 切镜检测直接使用无状态自由函数 `analyze_temporal_boundary()`，严禁恢复已物理删除的 `ShotGroupSegmenter` 类。  
2. **静态 IAT 保持 `<=` 3**：  
   * PE 头静态导入表（IAT）严格限定于 `{kernel32.dll, user32.dll, gdi32.dll}` 3 个核心库，严禁引入 `bcrypt.dll`、`kernelbase.dll` 或 `shlwapi.dll`。  
3. **纯 LGPL \+ SVT-AV1 宪法级合规**：  
   * 构建脚本与 FFmpeg 链接配置中严禁开启 `--enable-gpl` 或 `--enable-nonfree`。  
4. **受保护符号绝对不可丢失**：  
   * 确保 `consumeNvofSceneCut` 消费链与 `src/core/job_queue.cpp` 中的预览状态注入逻辑完整无损。

---

## **2\. Task 1：复现落地 MergeConcat 异构路由、areConcatInputsHomogeneous() 与固定 .mp4 预裁决兼容性矩阵**

### **Step 1.1 增量复现 areConcatInputsHomogeneous() 与 MergeConcat 异构路由**

在 `src/core/job_queue.cpp` 与 `src/io/ffmpeg_io.cpp` 中增量实现无状态自由函数 `areConcatInputsHomogeneous()`：

```
struct StreamAttributes {
    AVCodecID video_codec_id{AV_CODEC_ID_NONE};
    int width{0};
    int height{0};
    AVRational sar{0, 1};
    AVRational frame_rate{0, 1};
    AVPixelFormat pix_fmt{AV_PIX_FMT_NONE};
    int sample_rate{0};
    int channels{0};
    AVRational time_base{0, 1};
};

bool areConcatInputsHomogeneous(const std::vector<std::string>& input_paths) {
    if (input_paths.size() <= 1) return true;
    
    StreamAttributes base_attr;
    for (size_t i = 0; i < input_paths.size(); ++i) {
        AVFormatContext* fmt_ctx = nullptr;
        if (avformat_open_input(&fmt_ctx, input_paths[i].c_str(), nullptr, nullptr) < 0) {
            return false;
        }
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            avformat_close_input(&fmt_ctx);
            return false;
        }
        
        StreamAttributes current_attr;
        int v_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (v_idx >= 0) {
            auto* st = fmt_ctx->streams[v_idx];
            current_attr.video_codec_id = st->codecpar->codec_id;
            current_attr.width = st->codecpar->width;
            current_attr.height = st->codecpar->height;
            current_attr.sar = st->codecpar->sample_aspect_ratio;
            current_attr.frame_rate = av_guess_frame_rate(fmt_ctx, st, nullptr);
            current_attr.pix_fmt = static_cast<AVPixelFormat>(st->codecpar->format);
            current_attr.time_base = st->time_base;
        }
        
        int a_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (a_idx >= 0) {
            auto* st = fmt_ctx->streams[a_idx];
            current_attr.sample_rate = st->codecpar->sample_rate;
            current_attr.channels = st->codecpar->ch_layout.nb_channels;
        }
        
        avformat_close_input(&fmt_ctx);
        
        if (i == 0) {
            base_attr = current_attr;
        } else {

            if (base_attr.video_codec_id != current_attr.video_codec_id ||
                base_attr.width != current_attr.width ||
                base_attr.height != current_attr.height ||
                base_attr.sar.num * current_attr.sar.den != current_attr.sar.num * base_attr.sar.den ||
                base_attr.frame_rate.num * current_attr.frame_rate.den != current_attr.frame_rate.num * base_attr.frame_rate.den ||
                base_attr.pix_fmt != current_attr.pix_fmt ||
                base_attr.sample_rate != current_attr.sample_rate ||
                base_attr.channels != current_attr.channels) {
                return false;
            }
        }
    }
    return true;
}
```

#### **双路径路由逻辑**

1. **同构流极速直拼路径**：  
   * 当未挂载任何 AI / 色彩 / 缩放滤镜，且 `areConcatInputsHomogeneous(inputs) == true` 时，使用安全内存清单（In-Memory Concat List）驱动 FFmpeg `concat` Demuxer。  
   * 传入 `safe=0` 字典选项以完美支持盘符、空格及 UTF-8 路径。  
   * 执行流复制（`codec copy`）直拼，并在 Muxer 层强制注入 `movflags=+faststart` 输出 `.mp4`。  
2. **异构流无缝连续重编码合并路径**：  
   * 当 `areConcatInputsHomogeneous(inputs) == false` 或用户挂载了 AI 增强管线时，自动路由至 `runNvidiaRemasterRenderGraph()` 多解码器归一化管道（`hw_transcode_stages.cpp`）。  
   * 在 GPU CUDA 空间中执行等比缩放与居中黑边对齐（Letterbox/Pillarbox），将输入推入统一的目标画布尺寸。  
   * 按精确视频时长为无音轨片段补齐 48kHz 静音 PCM 缓冲，音频重采样器自动对齐声道数与采样率，并在单段结尾处推入 Null 样品修齐音画尾差。  
   * 全程无磁盘中间文件，数据流直通 NVENC / SVT-AV1 进行连续合并编码。

---

### **Step 1.2 固定 .mp4 输出的 resolve\_mp4\_stream\_plan() 兼容性预裁决矩阵**

进入渲染管线前，必须调用无状态自由函数 `resolve_mp4_stream_plan()` 结合 `avformat_query_codec()` 校验输入流，执行以下判定：

| 输入流类型 | 匹配条件 / 编码 ID | 处理策略与预裁决动作 | MP4 标记与参数要求 |
| :---- | :---- | :---- | :---- |
| **视频流** | H.264 / HEVC / AV1 | 直接封装（Stream Copy）或硬件/软件重编码 | HEVC 必须显式附加 `tag:v=hvc1` 标签，禁用 `sev1`，保障 Apple 平台原生兼容 |
| **MP4 原生兼容音轨** | AAC / Opus / FLAC / MP3 / AC-3 | 独立导出模式下默认流拷贝；在 `MergeConcat` 模式统一重编码对齐 | 经 `avformat_query_codec` 校验合法性，重编码统一采用 48kHz / 16-bit 采样对齐 |
| **MP4 不兼容音轨** | PCM（`pcm_s16le` 等）/ DTS / TrueHD / Vorbis | 绝不静默丢弃！执行确定性软解 \+ 重采样转码 | 默认转码为高码率 AAC；若配置了无损偏好则自动转码为 FLAC 音轨 |
| **文本字幕** | SRT / ASS / SSA / WebVTT | 显式转码为 MP4 规定格式 | 转换为 `mov_text` 流；弹出 Warning 提示高级样式与特效会被裁切 |
| **图像字幕与附件** | PGS / VobSub / DVB / MKV 内嵌 `.ttf`/`.otf` 字体 | 物理剥离与排除（Demuxer 忽略该 Stream） | 输出明确 Warning 日志，杜绝因无 Tag 映射导致的 MP4 Header 写入失败错误 |

```
enum class StreamHandlingPlan {
    DirectCopy,
    TranscodeToAAC,
    TranscodeToMovText,
    StripAndWarn
};

struct Mp4StreamDecision {
    int stream_index;
    AVMediaType type;
    StreamHandlingPlan plan;
    std::string warning_message;
};

std::vector<Mp4StreamDecision> resolve_mp4_stream_plan(AVFormatContext* fmt_ctx) {
    std::vector<Mp4StreamDecision> decisions;
    const AVOutputFormat* mp4_ofmt = av_guess_format("mp4", nullptr, nullptr);

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream* in_st = fmt_ctx->streams[i];
        AVCodecParameters* par = in_st->codecpar;
        Mp4StreamDecision decision{static_cast<int>(i), par->codec_type, StreamHandlingPlan::DirectCopy, ""};

        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            decision.plan = StreamHandlingPlan::DirectCopy;
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (avformat_query_codec(mp4_ofmt, par->codec_id, FF_COMPLIANCE_NORMAL) == 1) {
                decision.plan = StreamHandlingPlan::DirectCopy;
            } else {
                decision.plan = StreamHandlingPlan::TranscodeToAAC;
                decision.warning_message = "Non-MP4 audio codec detected. Transcoding to AAC.";
            }
        } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (par->codec_id == AV_CODEC_ID_SUBRIP || par->codec_id == AV_CODEC_ID_ASS || par->codec_id == AV_CODEC_ID_WEBVTT) {
                decision.plan = StreamHandlingPlan::TranscodeToMovText;
                decision.warning_message = "Text subtitle will be converted to mov_text. Text styling may be lost.";
            } else {
                decision.plan = StreamHandlingPlan::StripAndWarn;
                decision.warning_message = "Bitmap subtitle is incompatible with MP4 container and will be stripped.";
            }
        } else if (par->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
            decision.plan = StreamHandlingPlan::StripAndWarn;
            decision.warning_message = "Font attachment stripped for MP4 compatibility.";
        }
        decisions.push_back(decision);
    }
    return decisions;
}
```

---

### **Step 1.3 pure LGPL 解复用器扩展与 SVT-AV1 10-bit CRF 软编贯通**

在严格遵循纯 LGPL 许可的边界内，补充编译与注册以下扩展解复用器及软解码器：

* **扩展解复用器**：`asf`、`mpegps`、`webm`、`ivf`、`rm`。  
* **LGPL 软解码器**：`h264_decoder`、`hevc_decoder`、`vp9_decoder`、`mpeg2video_decoder`（补齐硬件 NVDEC 不支持的老旧或特殊格式）。

#### **SVT-AV1 10-bit 编码数据通路**

为实现无缝 10-bit AV1 编码，系统建立 `GpuToCpuStaging` 唯一显式 D2H 内存下传出口：

\$\$\\text{CUDA Surface (NV12 / P010)} \\xrightarrow{\\text{cudaMemcpy2DAsync (D2H)}} \\text{Host Staging Buffer} \\xrightarrow{\\text{SvtAv1Enc.lib}} \\text{AV1 Bitstream}\$\$

```
// GpuToCpuStaging -> SVT-AV1 10-bit CRF 写入逻辑
void stage_and_encode_svt_av1(const GpuBuffer& gpu_surface, SvtContext& svt_ctx, AVFormatContext* mp4_muxer) {
    // 1. D2H 异步拷贝至 Host 锁页内存
    cudaMemcpy2DAsync(svt_ctx.host_p010_buffer, svt_ctx.pitch,
                      gpu_surface.p010_ptr, gpu_surface.pitch,
                      svt_ctx.width * 2, svt_ctx.height * 3 / 2,
                      cudaMemcpyDeviceToHost, svt_ctx.stream);
    cudaStreamSynchronize(svt_ctx.stream);

    // 2. 填充 SVT-AV1 输入帧结构体
    EbBufferHeaderType* input_buffer = svt_ctx.raw_input_buffer;
    EbSvtIOFormat* in_img = reinterpret_cast<EbSvtIOFormat*>(input_buffer->p_buffer);
    in_img->y_buffer = static_cast<uint8_t*>(svt_ctx.host_p010_buffer);
    in_img->cb_buffer = in_img->y_buffer + (svt_ctx.pitch * svt_ctx.height);
    in_img->cr_buffer = in_img->cb_buffer + 1;

    // 3. 提交至 SvtAv1Enc.lib 编码
    svt_av1_enc_send_picture(svt_ctx.handle, input_buffer);
    
    // 4. 接收包并送入 MP4 Muxer (注入 faststart)
    EbBufferHeaderType* output_packet = nullptr;
    if (svt_av1_enc_get_packet(svt_ctx.handle, &output_packet, 0) == EB_ErrorNone) {
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data = output_packet->p_buffer;
        pkt.size = output_packet->n_filled_len;
        pkt.pts = output_packet->pts;
        pkt.dts = output_packet->dts;
        av_interleaved_write_frame(mp4_muxer, &pkt);
    }
}
```

---

## **3\. Task 2：队列任务独立隔离、`<chrono>` 1001 时基统一、预览槽位代数同步与公共切镜总线**

### **Step 2.1 队列独立隔离 & `<chrono>` 1001 时基**

#### **破坏性循环修复**

物理删除 `src/gui/rtx_gui.cpp:312-323` 中在用户点击"开始转码"时使用右侧 Panel 检查器参数覆写全队列 `Pending` 任务的错误代码。

配合"即拖即入队 \+ `active_sync_ids` 靶向联动"机制：每个任务在创建时即冻结快照其专属的 `JobSettings`，多任务间参数实现绝对隔离。

#### **`<chrono>` 1001 时基标准化**

彻底清理 `hw_transcode_stages.cpp:144` 等处遗留的硬编码浮点数或整数近似值，全库统一采用 C++20 `<chrono>` 编译期有理数类型：

```
// 声明 1001 NTSC 专属时间精度
using Timebase24fpsNtsc = std::chrono::duration<int64_t, std::ratio<1001, 24000>>;
using Timebase30fpsNtsc = std::chrono::duration<int64_t, std::ratio<1001, 30000>>;
using Timebase60fpsNtsc = std::chrono::duration<int64_t, std::ratio<1001, 60000>>;

// 示例：将呈现时间戳转换为标准 AV_TIME_BASE PTS
int64_t calculate_stream_pts(int64_t frame_index, Timebase24fpsNtsc) {
    auto duration = Timebase24fpsNtsc(frame_index);
    auto target_ticks = std::chrono::duration_cast<std::chrono::duration<int64_t, std::micro>>(duration);
    return target_ticks.count();
}
```

---

### **Step 2.2 预览槽位单调代数计数器、真实尺寸 Letterbox 与零开销暂停 CheckBox**

#### **槽位与代数同步**

核验 `src/core/job_queue.cpp:L359` 中的 `isPreviewEnabled()` 预览回调注入逻辑完好。

将 `preview_latest_slot_` 的初始状态从 `0` 重构为单调递增帧代数计数器 `preview_generation_`（未初始化状态设为 `0`，有效首帧从 `1` 开始）。UI 渲染端仅当检测到代数递增时才借用槽位，首帧未就绪前绝不误借槽位 0。

#### **物理 Letterbox 采样**

移除 UI 渲染逻辑中所有 `960x540` 与 `100.0f` 等硬编码数值。计算视口与源分辨率的真实等比缩放矩阵：

```
struct ViewportMatrix {
    float scale_x;
    float scale_y;
    float offset_x;
    float offset_y;
};

ViewportMatrix calculate_letterbox(uint32_t src_w, uint32_t src_h, uint32_t vp_w, uint32_t vp_h) {
    float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
    float vp_aspect = static_cast<float>(vp_w) / static_cast<float>(vp_h);
    
    ViewportMatrix mat{1.0f, 1.0f, 0.0f, 0.0f};
    if (src_aspect > vp_aspect) {
        // Pillarbox / Horizontal fit
        mat.scale_x = 1.0f;
        mat.scale_y = vp_aspect / src_aspect;
        mat.offset_y = (1.0f - mat.scale_y) * 0.5f;
    } else {
        // Letterbox / Vertical fit
        mat.scale_y = 1.0f;
        mat.scale_x = src_aspect / vp_aspect;
        mat.offset_x = (1.0f - mat.scale_x) * 0.5f;
    }
    return mat;
}
```

#### **CheckBox 零开销真旁路**

当用户在界面取消勾选预览 CheckBox 时，系统立即激活全链路真旁路（Bypass）机制：

1. **CUDA Pack Kernel** 跳过写出。  
2. **共享环形缓冲区（Ring Buffer）** 暂停写入。  
3. **30Hz Compute Dispatch** 渲染调度器停止触发。

彻底释放 GPU 算力与显存带宽消耗。

---

### **Step 2.3 无状态公共切镜与黑场总线 analyze\_temporal\_boundary()**

将原本仅挂载于 RIFE 路径的 8x8 分块 SAD 检测提取升级为解耦的无状态公共自由函数 `analyze_temporal_boundary()`，对解码后的原始 YUV 数据进行多维时域分析。

```
enum class TemporalBoundaryType {
    None,
    HardCut,
    FadeToBlack,
    HighMotion
};

TemporalBoundaryType analyze_temporal_boundary(const uint8_t* prev_y, const uint8_t* curr_y,
                                               int width, int height, int pitch) {
    // 1. 8x8 分块 SAD 计算
    uint64_t total_sad = 0;
    int blocks_x = width / 8;
    int blocks_y = height / 8;
    int changed_blocks = 0;

    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            uint32_t block_sad = 0;
            for (int y = 0; y < 8; ++y) {
                int idx = (by * 8 + y) * pitch + (bx * 8);
                for (int x = 0; x < 8; ++x) {
                    block_sad += std::abs(prev_y[idx + x] - curr_y[idx + x]);
                }
            }
            total_sad += block_sad;
            if (block_sad > (8 * 8 * 25)) { // 单块阈值
                changed_blocks++;
            }
        }
    }

    // 2. 亮度均值与方差计算
    uint64_t sum_y = 0;
    uint64_t sum_sq_y = 0;
    int total_pixels = width * height;
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            uint8_t val = curr_y[i * pitch + j];
            sum_y += val;
            sum_sq_y += val * val;
        }
    }
    double mean_y = static_cast<double>(sum_y) / total_pixels;
    double var_y = (static_cast<double>(sum_sq_y) / total_pixels) - (mean_y * mean_y);

    // 3. 融合判定逻辑
    if (mean_y < 15.0 && var_y < 10.0) {
        return TemporalBoundaryType::FadeToBlack;
    }
    
    double block_change_ratio = static_cast<double>(changed_blocks) / (blocks_x * blocks_y);
    if (block_change_ratio > 0.75) {
        return TemporalBoundaryType::HardCut;
    } else if (block_change_ratio > 0.40) {
        return TemporalBoundaryType::HighMotion;
    }

    return TemporalBoundaryType::None;
}
```

#### **下游组件切镜状态广播响应**

```
                             +-------------------------------+
                             | analyze_temporal_boundary()   |
                             +---------------+---------------+
                                             |
                   +-------------------------+-------------------------+
                   |                         |                         |
                   v                         v                         v
        +--------------------+    +--------------------+    +--------------------+
        |        RIFE        |    |       NVOFA        |    |      VSR / NVENC   |
        +--------------------+    +--------------------+    +--------------------+
        | 禁用光流插值       |    | 调用               |    | VSR: 清空历史帧    |
        | 插入硬切/黑场原帧   |    | consumeNvofSceneCut|    | NVENC: 强制发送    |
        +--------------------+    | 置位 Invalid 标志  |    |        IDR 帧      |
                                  +--------------------+    +--------------------+
```

1. **RIFE 插值**：遇到 `HardCut` 或 `FadeToBlack` 时，禁用光流混合，直接复制临界源帧。  
2. **NVOFA 光流**：打通 `consumeNvofSceneCut()` 消费链，自动置位 NVOFA 历史时间提示无效化标志，跳过当前帧光流矢量计算。  
3. **VSR 放大**：重置 Super Resolution 的 Lookback 历史帧缓存 Buffer，防止前一场景残影污染。  
4. **NVENC 编码器**：向编码器发送 `NV_ENC_PIC_PARAMS::encodePicFlags = NV_ENC_START_BASE_FLAG` 强制切出 IDR 关键帧。

---

## **4\. 验收 Gate (Checklist)**

为确保工程符合标准，编译完成后必须按顺序通过以下验收断言：

```
[ Gate 校验程序开始 ]
  [✓] 断言 1: 异构多片段 (不同分辨率 / 帧率 / 含无音轨片段) 提交 MergeConcat
      --> 检查输出: 生成单一 .mp4 视频，音画同步且全程无 PTS/DTS 撕裂告警。
  [✓] 断言 2: 包含 PGS 字幕与 DTS 音轨的 MKV 源文件转码 MP4
      --> 检查输出: PGS 字幕剥离并打印 Warning；DTS 成功转码 AAC，无静默丢音。
  [✓] 断言 3: SVT-AV1 10-bit CRF 软编实片测试
      --> 检查输出: 编码管线无报错，输出合法 faststart .mp4，FFprobe 识别为 main 10 profile。
  [✓] 断言 4: NVOFA 硬切与黑场测试
      --> 检查输出: 复杂切镜片段果冻效应彻底消失，日志确认 consumeNvofSceneCut 触发。
  [✓] 断言 5: 预览控制测试
      --> 检查输出: 动态勾选/取消预览 CheckBox 响应无延迟，旁路下 GPU 显存及 Compute 占用下降。
  [✓] 断言 6: 静态 IAT 导出表审计
      --> 运行 dumpbin /dependents rtx_remaster.exe 
      --> 结果: 仅包含 {kernel32.dll, user32.dll, gdi32.dll}，Static IAT Count == 3。
  [✓] 断言 7: 自动化测试套件
      --> 运行 rtx_tests.exe
      --> 结果: ALL TESTS PASSED (0 Failures).
[ 验收通过 - 准予合入 Wave B - Stage A ]
```

