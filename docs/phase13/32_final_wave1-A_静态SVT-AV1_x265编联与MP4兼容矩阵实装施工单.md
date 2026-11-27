# **32\_final\_wave1-A\_静态SVT-AV1\_x265编联与MP4兼容矩阵实装施工单**

## **1\. 波次定位与宪法红线**

### **1.1 施工波次定位**

* **阶段名称**：Final Wave 1-A（SVT-AV1/x265 静态编联与 MP4 兼容矩阵实装）  
* **硬性前置依赖**：依赖 `31_final_wave0-A` 宿主机 `rtx_tests.exe` 100% PASS 门禁后方可启动。本施工单承接 30 号裁决与 31 号全绿前置门禁。

### **1.2 宪法级构建红线**

| 检查项 | 约束规则与指标上限 | 校验与验证机制 |
| :---- | :---- | :---- |
| **AuditTypes** | `AuditTypes <= 34/35` | 零新增具名 `struct` / `class`，严格复用已有数据结构 |
| **AuditImports** | GUI 双模全绿 | `CineRTX.exe` GUI 静态导入物理 DLL 严格 `<= 4rtx_headless_cli.exe` 静态导入物理 DLL 严格 `== 2` |
| **AuditPrivacy** | `AuditPrivacy == 0` | 严禁在任何构建脚本、源文件或生成文档中包含本机盘符（如 `C:`、`D:`）或用户路径 |
| **许可证与封装** | 开源合规与格式锁定 | 默认内置 `libsvtav1`（BSD）作为全版本默认 AV1 软编核心；`libx265`（GPLv2+）由 `CINERTX_ENABLE_GPL_CODECS` 开关控制；物理容器锁定 `.mp4`（强制 `+faststart`） |

---

## **2\. Task 1：在 cmake/RtxFfmpegStatic.cmake 实装 SvtAv1Enc.lib（及 x265-static.lib）宿主机静态编译与编联**

**精准代码定位**：`cmake/RtxFfmpegStatic.cmake`（外部依赖静态构建与 FFmpeg configure 区域）

### **Step 1.1 静态归档构建配置**

在 `cmake/RtxFfmpegStatic.cmake` 中追加并实装 `ExternalProject_Add` 静态构建块，使其与主程序的 MSVC C/C++ 运行库（CRT）保持严格对齐：

```
# ==============================================================================
# SVT-AV1 Static Archive Configuration (BSD License - Default Built-in)
# ==============================================================================
ExternalProject_Add(
    dep_svtav1
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SVT-AV1"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/SVT-AV1-build"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_APPS=OFF
        -DBUILD_TESTING=OFF
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
        -DCMAKE_INSTALL_PREFIX=${THIRD_PARTY_DST}
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release
)

# ==============================================================================
# x265 Static Archive Configuration (GPLv2+ License - Optional Trigger)
# ==============================================================================
if(CINERTX_ENABLE_GPL_CODECS)
    ExternalProject_Add(
        dep_x265
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/x265/source"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/x265-build"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DENABLE_SHARED=OFF
            -DENABLE_CLI=OFF
            -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
            -DCMAKE_INSTALL_PREFIX=${THIRD_PARTY_DST}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release
    )
endif()
```

### **Step 1.2 FFmpeg configure 显式参数注入**

在 FFmpeg 静态配置列表 FFMPEG\_CONFIGURE\_FLAGS 中，显式注入 SVT-AV1 与可选的 x265 开关，并保持网络与无关协议全部禁用的隔离状态：

```
set(FFMPEG_CONFIGURE_FLAGS
    --enable-static
    --disable-shared
    --disable-programs
    --disable-autodetect
    --disable-network
    --disable-schannel
    --disable-protocols
    --enable-protocol=file,pipe
    --enable-libsvtav1
)

if(CINERTX_ENABLE_GPL_CODECS)
    list(APPEND FFMPEG_CONFIGURE_FLAGS --enable-gpl --enable-libx265)
endif()
```

### **Step 1.3 IAT 净空防泄漏检查**

确保 `SvtAv1Enc.lib` / `x265-static.lib` 全部作为纯静态归档链入最终目标，彻底切断 DLL 动态链接导出。

在构建完成阶段，执行以下审计脚本验证：h  
cmake \-P cmake/AuditImports.cmake

````

**准入硬性指标**：
* `CineRTX.exe` 依赖物理系统 DLL 数量 **`<= 4`** (`KERNEL32.dll`, `USER32.dll`, `SHELL32.dll`, `ADVAPI32.dll`)；
* `rtx_headless_cli.exe` 依赖物理系统 DLL 数量 **`== 2`** (`KERNEL32.dll`, `ADVAPI32.dll`)。

---

## 3. Task 2：打通 GpuToCpuStaging -> CpuVideoEncoder 实片 CRF 转码与混合硬/软解回落

### Step 2.1 GPU 到 CPU Staging 异步管线对齐
检查并修正 `src/core/hw_transcode_stages.cpp` 与 `src/io/cpu_video_encoder.cpp` 的帧推送逻辑。当编码配置指示 `options_.is_cpu == true` 时，启用 GPU 管线 NVMM 到 Host Pinned Memory 的双缓冲异步拷贝：

```cpp
// src/core/hw_transcode_stages.cpp
void StageGpuToCpuStaging::processFrame(const TranscodeFrame& inputFrame) {
    if (options_.is_cpu) {
        // CUDA Kernel: Convert RGB/NV12 Tensor to target YUV color surface
        cudaConvertPixelFormat(inputFrame.cuda_surface, staging_cuda_buffer_, 
                               inputFrame.width, inputFrame.height, 
                               options_.target_format); // NV12 or YUV420P10LE

        // Asynchronous Pinned Host Copy via Double Buffering
        int current_buf = double_buffer_index_ % 2;
        cudaMemcpyAsync(pinned_host_buffers_[current_buf].data, staging_cuda_buffer_,
                        staging_buffer_size_, cudaMemcpyDeviceToHost, 
                        stream_pool_[current_buf]);
        cudaStreamSynchronize(stream_pool_[current_buf]);

        // Hand over to CPU Video Encoder
        cpu_encoder_->encodeFrame(pinned_host_buffers_[current_buf], inputFrame.pts);
        double_buffer_index_++;
    }
}
````

## **Step 2.2 SVT-AV1 软编码器寻找与参数注入**

在 `src/io/cpu_video_encoder.cpp` 中，确立 `libsvtav1` 寻获机制与响应 `crf` / `preset` 参数赋值：

```
// src/io/cpu_video_encoder.cpp
bool CpuVideoEncoder::initializeEncoder(const EncoderOptions& options) {
    const AVCodec* codec = nullptr;
    if (options.codec_id == AV_CODEC_ID_AV1) {
        codec = avcodec_find_encoder_by_name("libsvtav1");
        if (!codec) {
            logError(I18N_CODEC_SVTAV1_NOT_FOUND);
            return false;
        }
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    codec_ctx_->width = options.width;
    codec_ctx_->height = options.height;
    codec_ctx_->pix_fmt = (options.bit_depth == 10) ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
    codec_ctx_->time_base = AVRational{1, options.fps};

    // Set CRF (0..63) and Preset
    av_opt_set(codec_ctx_->priv_data, "preset", std::to_string(options.preset).c_str(), 0);
    av_opt_set(codec_ctx_->priv_data, "crf", std::to_string(options.crf).c_str(), 0);

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        return false;
    }
    return true;
}
```

## **Step 2.3 RTX 5060 NVDEC 软解无缝回落机制**

在 `src/shim/loader/nv_decoder.cpp` 与 `src/io/ffmpeg_io.cpp` 中实装硬件解码能力检测。对于 RTX 5060 NVDEC 不原生支持的格式（如 `ProRes`、`WMV3`、`VP8`、`MPEG-2`），触发静默降级并回落至 CPU FFmpeg 软解总线：

```
// src/shim/loader/nv_decoder.cpp
bool NvDecoder::supportsCodec(AVCodecID codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_HEVC:
        case AV_CODEC_ID_AV1:
        case AV_CODEC_ID_VP9:
            return true;
        default:
            // Force Fallback for ProRes, WMV3, VP8, MPEG-2 etc.
            return false;
    }
}
```

---

# **4\. Task 3：实装固定 .mp4 音/字幕兼容矩阵并清理任何 \[\[maybe\_unused\]\]**

## **Step 3.1 严格矩阵判断机制实装**

在 `src/io/ffmpeg_io.cpp` 的 Muxer 初始化与流适配逻辑中，真实引用并调用 `isMp4DirectCopyAudioCodec()` 与 `isMp4TextSubtitleCodec()` 函数。**全工程彻底清除任何对上述函数的 `[[maybe_unused]]` 修饰，取消警告抑制，改由物理控制流绝对覆盖**。

```
// src/io/ffmpeg_io.cpp

bool isMp4DirectCopyAudioCodec(AVCodecID id) {
    return (id == AV_CODEC_ID_AAC || 
            id == AV_CODEC_ID_AC3 || 
            id == AV_CODEC_ID_EAC3 || 
            id == AV_CODEC_ID_MP3);
}

bool isMp4TextSubtitleCodec(AVCodecID id) {
    return (id == AV_CODEC_ID_SUBRIP || 
            id == AV_CODEC_ID_ASS || 
            id == AV_CODEC_ID_WEBVTT);
}

void SetupMp4CompatibilityMatrix(AVFormatContext* out_ctx, AVStream* in_stream, bool is_edited) {
    AVCodecParameters* in_par = in_stream->codecpar;

    if (in_par->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (!is_edited && isMp4DirectCopyAudioCodec(in_par->codec_id)) {
            // Passthrough Mode: Direct Copy
            AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
            avcodec_parameters_copy(out_stream->codecpar, in_par);
        } else {
            // Transcode Mode: Re-encode to AAC (Covers FLAC/Opus/Vorbis/PCM/DTS/TrueHD/WMA)
            AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
            InitAacEncoderStream(out_stream, in_par);
        }
    } else if (in_par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        if (isMp4TextSubtitleCodec(in_par->codec_id)) {
            // Convert SRT/ASS/WebVTT to MP4 mov_text
            AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
            out_stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
            out_stream->codecpar->codec_id = AV_CODEC_ID_MOV_TEXT;
        } else {
            // Bitmap Subtitles (PGS/DVDSub/DVBSub): Skip & Log i18n message without breaking container
            LogStructuredI18nWarning(I18N_SUBTITLE_BITMAP_SKIPPED, 
                                     avcodec_get_name(in_par->codec_id));
        }
    }
}
```

---

# **5\. 宿主机真机验收 Gate (Acceptance Checklist)**

## **5.1 自动化编译构建**

- [ ] 宿主机根目录下直接执行 `.\build.bat` 必须返回退出码 `0`。  
- [ ] 全程无 C4100 / C4189 等编译警告，所有 `[[maybe_unused]]` 标注彻底清除。

## **5.2 软件编码实片与 Faststart 校验**

- [ ] 执行 `av1_cpu` 模式转码测试：

```
rtx_headless_cli.exe --input input.mp4 --codec av1_cpu --crf 30 --output output_av1.mp4
```

- [ ] 执行 `hevc_cpu` 模式转码测试：

```
rtx_headless_cli.exe --input input.mp4 --codec hevc_cpu --crf 24 --output output_hevc.mp4
```

- [ ] 使用 FFprobe 检验输出文件流结构，校验 `faststart` 头元数据是否放置于文件头部：

```
ffprobe -v error -show_format -show_streams output_av1.mp4
```

  `output_av1.mp4` 必须确认包含 `av01` 视频编码轨且 moov atom 居于 mdat 之前。

## **5.3 音/字幕兼容矩阵与异构格式转换测试**

- [ ] **异构音频/字幕文件测试**：输入包含 `FLAC` 音轨 \+ `PGS` 字幕的 `.mkv` 容器文件：  
  * 转码产物中音频被正确重编码为 `AAC` 且可正常播放；  
  * `PGS` 位图字幕被安全跳过，输出日志正确呈现 i18n 警告说明，封装流程不中断、不崩溃。  
- [ ] **老旧容器输入测试**：输入 `.wmv` (WMV3/WMA) 或 `.webm` (VP8/Vorbis) 文件：  
  * 解码器无缝降级回落至 FFmpeg CPU 软解；  
  * 输出文件完美转换为符合 `.mp4` 规范的合法视频文件。

## **5.4 静态安全与单元测试全绿闭环**

- [ ] 执行单元测试集 `.\rtx_tests.exe`，达成 **100% PASS**（全绿通过）。  
- [ ] 执行 `cmake -P cmake/AuditImports.cmake`：  
  * `CineRTX.exe` 静态导入物理 DLL **`<= 4`**；  
  * `rtx_headless_cli.exe` 静态导入物理 DLL **`== 2`**。  
- [ ] 运行 `AuditPrivacy` 检查，报告显露绝对零绝对路径引用（**`AuditPrivacy == 0`**）。

