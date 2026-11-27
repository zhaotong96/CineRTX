# **Wave 1-B 全容器扩展、SVT-AV1 软编 CRF 与 MP4 兼容交付施工单**

Superpowers 原子执行规约（与 Wave 1-A 完全并行工程）

## **1\. 元信息与核心控制目标**

### **1.1 施工上下文**

* **施工波次**：Wave 1 – Stage B（全容器输入矩阵、混合硬/软解路由、CPU CRF 软编轨道重接、`faststart` 固定 `.mp4` 容器规范化交付）。  
* **并行约束**：与 Wave 1-A（NVENC 极致硬件编码管道）高度解耦，并行推进。不得修改 Wave 1-A 专属接口与独占数据流。

### **1.2 宪法级合规红线**

#### **红线一：具名类型配额 (Nominal Type Quota)**

* **硬性约束**：全局具名类型总数必须严格控制为 `AuditTypes <= 34/35`。  
* **执行手段**：严禁定义任何新的 `struct`、`class`、`enum` 或 `union`。所有新逻辑必须通过以下途径实现：  
  1. 扩展既有表驱动静态数据结构：`TranscodeProfile` 实例表、`ParamSchema` 参数绑定数组。  
  2. 声明自由函数（Free Functions）、匿名命名空间内部 Helper 函数或重用现有泛型 lambda。  
  3. 利用既有元组 `std::tuple` 或基础数据结构承载临时状态。

#### **红线二：静态编联与许可证合规 (Static Linkage & Compliance)**

参照 `docs/spec/compliance.md` 与 `cmake/RtxFfmpegStatic.cmake` 规定：

* **主程序许可证**：`GPL-3.0-or-later WITH Plugin-Exception`。  
* **FFmpeg 编译参数**：严格维持 `BUILD_FFMPEG_STATIC` 标准：  
  `--disable-network --disable-schannel --disable-protocols --enable-protocol=file,pipe --disable-autodetect --disable-programs --disable-shared --enable-static`  
* **CRT 模式一致性**：所有引入的静态第三方库（包含 `libsvtav1` / `libx265`）必须使用与主程序完全一致的 C 运行时库模式（统一 `/MT` 或统一 `/MD`），严禁出现 `/NODEFAULTLIB` 冲突或多版本 MSVCRT 混用。  
* **专有 SDK 物理隔离**：TensorRT、NVRTI 及 NGX 等专有非自由动态库，严格物理隔离在 `rtx_ngx_bridge.dll` 边界之后，严禁直接静态引入主二进制。

---

## **2\. Task 1：全格式容器解复用矩阵与"NVDEC 优先 \+ 轻量软解回落"统一帧入口**

### **2.1 源码诊断与缺陷定位**

* **编译配置缺陷**：`cmake/RtxFfmpegStatic.cmake:379` 的 `FFMPEG_DEMUXERS` 列表中仅开启了 `mov,matroska,mpegts,flv,avi,mxf,aac,mp3,flac,ogg,wav,w64`。  
* **解码映射缺陷**：`src/shim/loader/nv_decoder.cpp:47-58` 的硬件解码映射仅涵盖 H.264、HEVC、AV1 三类。  
* **UI 交互缺陷**：`src/gui/rtx_gui.cpp:357, 475` 文件选择对话框写死后缀 `*.mp4;*.mkv;*.mov;*.ts`，阻断了其他合法格式的加载。

### **2.2 原子级施工步骤**

#### **Step 1.1：静态编译配置补全 (`cmake/RtxFfmpegStatic.cmake`)**

在 `FFMPEG_DEMUXERS`、`FFMPEG_PARSERS` 及 `FFMPEG_DECODERS` 中扩充以下白名单：

```
# 1. 扩充 Demuxers
list(APPEND FFMPEG_DEMUXERS 
    asf mpegps rm ivf webm dv gif
)

# 2. 扩充 Parsers
list(APPEND FFMPEG_PARSERS 
    h264 hevc av1 vp8 vp9 mpeg4video mpegvideo vc1
)

# 3. 扩充 NVDEC 硬件解码器与轻量化 CPU 解码器
list(APPEND FFMPEG_DECODERS 
    # NVDEC 硬件加速
    h264_cuvid hevc_cuvid av1_cuvid vp8_cuvid vp9_cuvid mpeg2_cuvid mpeg4_cuvid vc1_cuvid
    # 轻量 CPU 软解回落 (含 libdav1d)
    libdav1d h264 hevc vp8 vp9 mpeg2video mpeg4 vc1 wmv3 prores
)
```

#### **Step 1.2：统一帧路由引擎设计 (`src/decoder/demux_decode_pipeline.cpp`)**

构建"NVDEC 硬件优先 \$\\rightarrow\$ CPU 轻量软解 \$\\rightarrow\$ CUDA 显存规范化"统一路由。

```
                     [输入媒体文件 (全容器支持)]
                                 │
                         (Demuxer 矩阵解包)
                                 │
                         [压缩视频数据包]
                                 │
                 ┌───────────────┴───────────────┐
         (检测 NVDEC 支持?)                   (不支持)
          4:2:0 & 8/10bit                4:2:2/ProRes/WMV3等
                 │                               │
                 ▼                               ▼
       [NVDEC 硬件解码]                 [FFmpeg CPU 软解]
                 │                               │
        (Direct GPU Frame)               (Host System RAM)
                 │                               │
                 │                     (CUDA Staging 异步上传)
                 │                               │
                 └───────────────┬───────────────┘
                                 │
                       [统一 GPU 帧总线]
                      (NV12 / P016 格式)
             *标记 诊断标签: is_sw_decoded = true/false
                                 │
                                 ▼
                     下游 VSR / RIFE / TrueHDR
```

实现代码示例（利用自由函数，禁止新增具名结构体）：

```
namespace {
// 自由函数：判断 NVDEC 是否原生支持指定 Codec 与 色彩格式
bool check_nvdec_compatibility(AVCodecID codec_id, AVPixelFormat pix_fmt, int bit_depth) {
    if (bit_depth > 10) return false;

    if (pix_fmt == AV_PIX_FMT_YUV422P || pix_fmt == AV_PIX_FMT_YUV422P10LE || pix_fmt == AV_PIX_FMT_YUV444P) {
|---|---|---|false; // 硬件显卡限制：转入 CPU 软解
    }
    switch (codec_id) {
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_HEVC:
        case AV_CODEC_ID_AV1:
        case AV_CODEC_ID_VP9:
            return true;
        default:
            return false;
    }
}

// 自由函数：CPU 帧上传至 CUDA 显存规范化总线
bool upload_cpu_frame_to_cuda(const AVFrame* sw_frame, CUdeviceptr out_dptr, int out_pitch, bool& out_is_sw) {
    out_is_sw = true;
    // 依据 sw_frame->format (YUV420P / YUV422P / RGB 等) 执行 Host -> Device 拷贝并转码为 NV12/P016
    // 详细 CUDA Kernel: convert_sw_frame_to_nv12_p016_async<<<...>>>(...);
    return true;
}
} // namespace
```

#### **Step 1.3：UI 文件筛选器组扩充 (`src/gui/rtx_gui.cpp`)**

更新第 357 及 475 行文件选择器中的 Filter 字符串：p  
// 替换原有的硬编码后缀  
const char\* media\_filter \=  
"All Supported Media (*.mp4;*.mkv;*.mov;*.webm;*.avi;*.ts;*.m2ts;*.flv;*.mxf;*.wmv;*.asf;*.mpg;*.mpeg;*.vob;*.rmvb;*.ivf)\\0"  
".mp4;.mkv;.mov;.webm;.avi;.ts;.m2ts;.flv;.mxf;.wmv;.asf;.mpg;.mpeg;.vob;.rmvb;.ivf\\0"  
"MP4 Video (*.mp4)\\0*.mp4\\0"  
"MKV Video (.mkv)\\0.mkv\\0"  
"WebM Video (.webm)\\0.webm\\0"  
"All Files (*.*)\\0\*.\*\\0\\0";

````

---

## 3. Task 2：固定 `.mp4` 单一输出容器兼容矩阵

### 3.1 架构定案
* **交付原则**：输出封装容器**物理锁定为 `.mp4`**。
* **Faststart 原规约**：在调用 `avformat_write_header` 前，必须通过 `av_dict_set(&options, "movflags", "+faststart", 0)` 将 `moov` 原子移动至文件头部，确保流媒体就绪。

### 3.2 多流路由处理矩阵

| 流类型 | 输入流特征 | 映射规则 / 路由动作 | 参数与技术细节 |
| :--- | :--- | :--- | :--- |
| **视频流** | H.264 / HEVC / AV1 | **直接写入 / Stream Copy 或转码** | 透传 `color_primaries`, `color_trc`, `colorspace` 元数据。 |
| **音频流** | AAC / AC-3 / E-AC-3 / MP3 | **零损直通 (Stream Copy)** | 保持原有采样率与声道布局，仅在无需重采样时使用。 |
| **音频流** | PCM / FLAC / Opus / DTS / TrueHD / WMA / Vorbis | **自动重采样并转码为 AAC** | 使用 FFmpeg 内置 `aac` 编码器。<br/>立体声 $\rightarrow$ `256 kbps`；5.1 声道 $\rightarrow$ `512 kbps`。<br/>*严禁引入 nonfree `libfdk_aac`*。 |
| **字幕流** | SRT / ASS / SSA / WebVTT | **封装转码为 `mov_text`** | 格式映射至标准 MP4 `tx3g` 轨道。 |
| **字幕流** | PGS / DVDSub / DVBSub | **安全忽略并记录提示** | 因 ISO BMFF 规范阻断位图字幕，自动跳过并在系统日志及 UI 状态栏输出结构化 i18n 警告，防止 `avformat_write_header` 报错崩溃。 |

### 3.3 核心实现代码规约 (`src/muxer/mp4_muxer_stage.cpp`)

```cpp
namespace {
// 自由函数：音频兼容性路由与转码推导
bool setup_mp4_audio_stream(AVFormatContext* ofmt_ctx, const AVStream* in_stream, AVStream* out_stream, AVDictionary** mux_opts) {
    AVCodecID in_codec_id = in_stream->codecpar->codec_id;
    
    // 判断是否可直接 Stream Copy
    if (in_codec_id == AV_CODEC_ID_AAC || in_codec_id == AV_CODEC_ID_AC3 || 
        in_codec_id == AV_CODEC_ID_EAC3 || in_codec_id == AV_CODEC_ID_MP3) {
        avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        out_stream->codecpar->codec_tag = 0;
        return false; // 不需要重新解码/编码
    }

    // 需路由至内置 AAC 编码器
    const AVCodec* aac_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    out_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    out_stream->codecpar->codec_id = AV_CODEC_ID_AAC;
    out_stream->codecpar->sample_rate = in_stream->codecpar->sample_rate;
    out_stream->codecpar->ch_layout = in_stream->codecpar->ch_layout;
    
    // 声道自适应码率分配
    int channels = in_stream->codecpar->ch_layout.nb_channels;
    out_stream->codecpar->bit_rate = (channels > 2) ? 512000 : 256000;
    return true; // 需要启动音频转码管线
}

// 自由函数：字幕流兼容性判定
bool setup_mp4_subtitle_stream(AVFormatContext* ofmt_ctx, const AVStream* in_stream, AVStream* out_stream) {
    AVCodecID in_codec_id = in_stream->codecpar->codec_id;
    if (in_codec_id == AV_CODEC_ID_SUBRIP || in_codec_id == AV_CODEC_ID_ASS || 
        in_codec_id == AV_CODEC_ID_SSA || in_codec_id == AV_CODEC_ID_WEBVTT) {
        out_stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        out_stream->codecpar->codec_id = AV_CODEC_ID_MOV_TEXT;
        return true;
    }
    
    // 位图字幕跳过逻辑
    if (in_codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE || in_codec_id == AV_CODEC_ID_DVD_SUBTITLE) {
        // 输出结构化 i18n 警告信息
        log_i18n_warning("WARN_SUBTITLE_BITMAP_SKIPPED_FOR_MP4", in_stream->index);
        return false; // 丢弃该流，不打断封装流程
    }
    return false;
}
} // namespace
````

---

# **4\. Task 3：打通 CPU 软编码轨道（`libsvtav1` / `libx265` CRF）与 GPU \$\\rightarrow\$ CPU 安全下传**

## **4.1 源码诊断与缺陷定位**

* **参数校验阻断**：`src/core/param_schema.cpp:184-209, 291` 显式拦截了 `av1_cpu` 与 `hevc_cpu`，直接返回 `Unsupported`，并将 `crf` 强制偷换为 `cq`。  
* **硬件标记错误**：`profileToHwOptions` 内写死 `const bool is_cpu = false;`。  
* **编译选项缺失**：`RtxFfmpegStatic.cmake` 禁用了所有 CPU 编码器。

## **4.2 施工细节与步骤**

### **Step 3.1：静态库整合与静态编译更新 (`cmake/RtxFfmpegStatic.cmake`)**

静态启用 `libsvtav1`（SVT-AV1, BSD 许可）与 `libx265`（x265, GPL 许可）：

```
## 启用第三方软编静态库支持
list(APPEND FFMPEG_ENCODERS 
    libsvtav1 libx265
)

## 链接系统依附库，防止静态符号丢失（确保 AuditImports 校验通过）
if(WIN32)
    list(APPEND SYSTEM_LINK_LIBRARIES
        ws2_32 bcrypt secur32 mfplat dxva2
    )
endif()
```

### **Step 3.2：解除硬编码阻断 (`src/core/param_schema.cpp`)**

修正 `param_schema.cpp` 中对 `av1_cpu` 和 `hevc_cpu` 的路由逻辑：

```
// 修正前的偷换与阻断代码需移除，更新为如下合法透传逻辑：
bool validate_and_map_encoding_params(const ProfileConfig& prof, EncoderOptions& out_opts) {
    if (prof.encoder_name == "av1_cpu") {
        out_opts.codec_name = "libsvtav1";
        out_opts.is_cpu = true;
        out_opts.crf = std::clamp(prof.crf_value, 0, 63); // SVT-AV1 合法 CRF 范围 0..63
        out_opts.preset = prof.preset_str; // e.g. "5", "6"
        return true;
    } else if (prof.encoder_name == "hevc_cpu") {
        out_opts.codec_name = "libx265";
        out_opts.is_cpu = true;
        out_opts.crf = std::clamp(prof.crf_value, 0, 51); // x265 合法 CRF 范围 0..51
        out_opts.preset = prof.preset_str; // e.g. "medium", "slow"
        return true;
    }
    // NVENC 逻辑路径保持不变...
    return true;
}
```

### **Step 3.3：异步 `GpuToCpuStaging` 内存下传管线 (`src/pipeline/hw_transcode_stages.cpp`)**

在 GPU 处理链末端构造双缓冲 Pinned Memory 下传流，解决 GPU 与 CPU 多线程编码的流水线阻塞问题。

```
 [GPU 渲染/推理处理] 
 (TensorRT / VSR / RIFE)
           │
           ▼
 [GPU 色彩转换 CUDA Kernel] 
 (转为 NV12 / P010 / YUV420P10LE)
           │
           ▼
 [Pinned Host Memory (双缓冲池)]
 ┌───────────────────┬───────────────────┐
 │ Buffer A (Slot 0) │ Buffer B (Slot 1) │
 └───────────────────┴───────────────────┘
           │ (cudaMemcpyAsync - D2H)
           ▼
 [CpuVideoEncoder (多线程并发)]
 (libsvtav1 / libx265 编码线程池)
```

代码逻辑实现（使用自由函数）：

```
namespace {
// 自由函数：GPU 到 Host Pinned Memory 的异步传输
void async_stage_gpu_to_cpu(CUdeviceptr d_src, size_t src_pitch, void* h_pinned_dst, 
                            int width, int height, cudaStream_t stream) {
    CUDA_MEMCPY2D copy_params = {};
    copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.srcDevice     = d_src;
    copy_params.srcPitch      = src_pitch;
    
    copy_params.dstMemoryType = CU_MEMORYTYPE_HOST;
    copy_params.dstHost       = h_pinned_dst;
    copy_params.dstPitch      = width; // 紧凑排布
    
    copy_params.WidthInBytes  = width;
    copy_params.Height        = height;
    
    cuMemcpy2DAsync(&copy_params, stream);
}
} // namespace
```

---

# **5\. 验收门禁与单测覆盖矩阵**

```
                             [验收门禁自动化测试]
                                       │
         ┌─────────────────────────────┼─────────────────────────────┐
         ▼                             ▼                             ▼
  [异构容器样本测试]            [CPU 软编端到端单测]           [静态合规与架构审计]
 (WMV/ProRes/WebM等)          (SVT-AV1 / x265 CRF)           (AuditTypes/Imports)
         │                             │                             │
         ▼                             ▼                             ▼
  输出 faststart .mp4            验证码流/CRF质量匹配          全绿通过 (<=34/35)
```

## **5.1 测试用例配置矩阵**

| 测试编号 | 输入异构样本格式 | 目标编码与配置 | 预期行为与关键指标 | 验证命令 / 判定准则 |
| :---- | :---- | :---- | :---- | :---- |
| **TC-1B-01** | `sample_vc1.wmv` (WMV/ASF 容器) | NVENC HEVC \+ AAC | 自动识别并软解 VC-1，CUDA 上传，生成规范 MP4。 | `ffprobe -v error -show_entries format=format_name out.mp4` 包含 `mov,mp4` |
| **TC-1B-02** | `sample_prores.mov` (ProRes 422\) | `av1_cpu` (CRF=28) | 软解 4:2:2，CUDA 转 NV12，SVT-AV1 软编输出。 | 检查码流是否符合 CRF=28 的动态码率特征，`moov` 前置。 |
| **TC-1B-03** | `sample_vp9.webm` (VP9 \+ Opus) | `hevc_cpu` (CRF=22) | VP9 解码，Opus 自动重采样并转码为 AAC (256k)。 | 音轨Codec ID标识为 `aac`，无丢音现象。 |
| **TC-1B-04** | `sample_flac.mkv` (FLAC 音轨) | NVENC H.264 \+ Stream Copy | 视频 Copy/转码，FLAC 自动路由转码为 AAC 5.1 (512k)。 | 音频无失真，通道映射正确。 |
| **TC-1B-05** | `sample_pgs.mkv` (PGS 字幕) | NVENC HEVC | 识别 PGS，记录 i18n 跳过日志，正常生成 MP4。 | 封装流程零崩溃，日志包含 `WARN_SUBTITLE_BITMAP_SKIPPED` |
| **TC-1B-06** | 静态架构审计 | 全域类型与导入库扫描 | 类型配额 `AuditTypes <= 34/35`，导入表干净。 | 执行 `python scripts/audit_symbol_quota.py` 结果全绿。 |

---

# **6\. 施工准备与实施操作单**

## **6.1 前置准备**

1. 确认工具链：MSVC 2022 / CUDA Toolkit 12.x / CMake 3.26+。  
2. 校验静态依赖库架构为 x64，且运行时模式统一为 `/MT`。

## **6.2 步骤执行顺序**

```
Step 1.1 - 1.3: 静态 CMake 扩展 & 解码层统一路
```

由 & GUI 筛选器修改  
│  
▼  
Step 2.1 \- 2.3: 固定 MP4 封装 & 自动音频重采样 & 字幕安全路由  
│  
▼  
Step 3.1 \- 3.3: 静态集成 libsvtav1/libx265 & 参数阻断解除 & GpuToCpuStaging 异步管线  
│  
▼  
Step 5.1 \- 5.2: 运行测试用例 TC-1B-01 至 TC-1B-06，确保 Audit 静态审计与端到端转码全部 Pass