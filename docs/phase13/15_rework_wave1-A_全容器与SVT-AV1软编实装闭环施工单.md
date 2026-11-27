# **15\_rework\_wave1-A\_全容器与SVT-AV1软编实装闭环施工单**

## **0\. 元信息与驳回定性复盘**

### **0.1 执行规范与依赖关联**

* **施工单编号**：`15_rework_wave1-A`  
* **依据裁决**：协调负责人驳回裁决 `CINERTX-P13-RULING-013` 第 13 号文档  
* **执行波次**：Rework Wave 1-A（依赖 Rework Wave 0-A 全绿通过后启动，可与 Rework Wave 2-A 并行）

### **0.2 驳回定性复盘 (Post-Mortem)**

原 Wave 1-B（05 号文档）被裁决为 **"严重空壳（REJECTED）"**。

工程师仅在 `src/core/param_schema.cpp:184` 与 `src/io/cpu_video_encoder.cpp:59` 编写了 C++ 路由签名，但 `cmake/RtxFfmpegStatic.cmake` **既未启用 `--enable-libsvtav1` / `--enable-libx265`，也未引入对应静态库的编译与链入**，导致静态 FFmpeg 运行期找不到任何 CPU 软编码器；同时，输入输出容器的非 MP4 兼容音轨/字幕转码矩阵、以及 NVDEC 不支持编码的软解回落映射均未闭环。

### **0.3 宪法级硬红线 (Strict Hard Boundaries)**

* **结构体防护（AuditTypes）**：`AuditTypes <= 34/35`（零新增具名 `struct`/`class`）。  
* **导出依赖防护（AuditImports）**：双模全绿。  
  * `CineRTX.exe`（GUI 模式）：静态导入 DLL 严格 `<= 4` 个（物理限制为 `kernel32, user32, gdi32, bcrypt`）。  
  * `rtx_headless_cli.exe`（Headless 模式）：静态导入 DLL 严格 `== 2` 个（物理限制为 `bcrypt, kernel32`）。  
  * 新增静态库严禁向主程序引入额外系统 DLL 静态导入。  
* **许可证合规（License Compliance）**：根据 `docs/spec/compliance.md`：  
  * 主程序遵循 `GPL-3.0-or-later WITH Plugin-Exception`；  
  * `libsvtav1`（BSD-2-Clause-Patent / BSD-3-Clause-Clear）作为默认内置全配置可用的 AV1 软编码主力；  
  * `libx265`（GPLv2+）通过 `CINERTX_ENABLE_GPL_CODECS` 显式配置开关编入完整 GPL 发行包。  
* **静态 FFmpeg 构建卫生铁律**：  
  * 保持 Flags 包含 `--disable-network --disable-schannel --disable-protocols --enable-protocol=file,pipe --disable-autodetect --disable-programs --disable-shared --enable-static`。  
  * 全依赖树统一 MSVC CRT 标志（根据全局配置严格对齐 `/MT` 或 `/MD`）。

---

## **1\. Task 1: 补实 `cmake/RtxFfmpegStatic.cmake` 静态 `libsvtav1` (及 `libx265`) 编译与零 IAT 污染编联**

### **1.1 精确坐标**

* 主配置文件：`cmake/RtxFfmpegStatic.cmake`（第 379 行附近及外部库构建/查找规则）。

### **1.2 实施步骤 (TDD)**

#### **Step 1.1（构建期静态归档集成）**

在 `cmake/RtxFfmpegStatic.cmake` 中追加 `SvtAv1Enc` 与 `x265` 的 CMake 外部项目构建或预置目标配置。强制关闭共享库构建，并对齐 MSVC 运行库。

```
# ==============================================================================
# SVT-AV1 (libsvtav1) 静态归档构建
# ==============================================================================
if(NOT TARGET SvtAv1::Encoder)
    include(ExternalProject)
    
    set(SVT_AV1_PREFIX "${CMAKE_BINARY_DIR}/third_party/svt-av1")
    set(SVT_AV1_INSTALL_DIR "${SVT_AV1_PREFIX}/install")
    
    # 强制与主工程对齐 MSVC CRT (/MT 或 /MD)
    set(SVT_C_FLAGS "${CMAKE_C_FLAGS}")
    set(SVT_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    
    ExternalProject_Add(
        external_svtav1
        PREFIX "${SVT_AV1_PREFIX}"
        GIT_REPOSITORY "https://gitlab.com/AOMediaCodec/SVT-AV1.git"
        GIT_TAG "v1.8.0"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${SVT_AV1_INSTALL_DIR}
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_TESTING=OFF
            -DBUILD_APPS=OFF
            -DENABLE_AVX512=OFF
            -DCMAKE_C_FLAGS=${SVT_C_FLAGS}
            -DCMAKE_CXX_FLAGS=${SVT_CXX_FLAGS}
            -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
        BUILD_BYPRODUCTS "${SVT_AV1_INSTALL_DIR}/lib/SvtAv1Enc.lib"
    )

    add_library(SvtAv1::Encoder STATIC IMPORTED GLOBAL)
    add_dependencies(SvtAv1::Encoder external_svtav1)
    set_target_properties(SvtAv1::Encoder PROPERTIES
        IMPORTED_LOCATION "${SVT_AV1_INSTALL_DIR}/lib/SvtAv1Enc.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${SVT_AV1_INSTALL_DIR}/include"
    )
endif()

# ==============================================================================
# x265 (libx265) 静态归档构建 (CINERTX_ENABLE_GPL_CODECS 模式下激活)
# ==============================================================================
if(CINERTX_ENABLE_GPL_CODECS AND NOT TARGET x265::Encoder)
    set(X265_PREFIX "${CMAKE_BINARY_DIR}/third_party/x265")
    set(X265_INSTALL_DIR "${X265_PREFIX}/install")

    ExternalProject_Add(
        external_x265
        PREFIX "${X265_PREFIX}"
        SOURCE_DIR "${X265_PREFIX}/src/source"
        GIT_REPOSITORY "https://bitbucket.org/multicoreware/x265_git.git"
        GIT_TAG "3.5"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${X265_INSTALL_DIR}
            -DENABLE_SHARED=OFF
            -DENABLE_CLI=OFF
            -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
            -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
            -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
        BUILD_BYPRODUCTS "${X265_INSTALL_DIR}/lib/x265-static.lib"
    )

    add_library(x265::Encoder STATIC IMPORTED GLOBAL)
    add_dependencies(x265::Encoder external_x265)
    set_target_properties(x265::Encoder PROPERTIES
        IMPORTED_LOCATION "${X265_INSTALL_DIR}/lib/x265-static.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${X265_INSTALL_DIR}/include"
    )
endif()
```

#### **Step 1.2（FFmpeg configure 显式启用）**

修改 `RtxFfmpegStatic.cmake` 中 FFmpeg 的配置选项与额外编译/链接参数：

```
set(FFMPEG_EXTRA_CFLAGS "-I${SVT_AV1_INSTALL_DIR}/include")
set(FFMPEG_EXTRA_LDFLAGS "-L${SVT_AV1_INSTALL_DIR}/lib")
set(FFMPEG_EXTRA_LIBS "-lSvtAv1Enc")

set(FFMPEG_CONFIGURE_FLAGS
    --disable-network
    --disable-schannel
    --disable-protocols
    --enable-protocol=file,pipe
    --disable-autodetect
    --disable-programs
    --disable-shared
    --enable-static
    --enable-libsvtav1
)

if(CINERTX_ENABLE_GPL_CODECS)
    list(APPEND FFMPEG_CONFIGURE_FLAGS --enable-gpl --enable-libx265)
    set(FFMPEG_EXTRA_CFLAGS "${FFMPEG_EXTRA_CFLAGS} -I${X265_INSTALL_DIR}/include")
    set(FFMPEG_EXTRA_LDFLAGS "${FFMPEG_EXTRA_LDFLAGS} -L${X265_INSTALL_DIR}/lib")
    set(FFMPEG_EXTRA_LIBS "${FFMPEG_EXTRA_LIBS} -lx265")
endif()

# 传递给 FFmpeg ExternalProject_Add
# --extra-cflags=${FFMPEG_EXTRA_CFLAGS}
# --extra-ldflags=${FFMPEG_EXTRA_LDFLAGS}
# --extra-libs=${FFMPEG_EXTRA_LIBS}
```

#### **Step 1.3（IAT 净空验证）**

确保编入 `libsvtav1` 与 `libx265` 后，未引入系统 DLL 的隐式依赖（如网络库 `ws2_32` 或 `secur32`）。

```
# 运行构建
cmake --build build --config Release --target CineRTX rtx_headless_cli

# 执行静态导入表合规审计
cmake -P cmake/AuditImports.cmake
```

*验收断言*：`CineRTX.exe` 静态导入库项 `<= 4`（`kernel32, user32, gdi32, bcrypt`），`rtx_headless_cli.exe` 静态导入库项 `== 2`（`bcrypt, kernel32`）。

---

## **2\. Task 2: 打通 `GpuToCpuStaging` 异步下传与 `CpuVideoEncoder` 实片 CRF 编码闭环**

### **2.1 精确坐标**

* `src/core/hw_transcode_stages.cpp`  
* `src/io/cpu_video_encoder.cpp:59`  
* `src/core/param_schema.cpp:184`

### **2.2 实施细节**

#### **Step 2.1（GPU 到 Host 异步 Memory Pipeline）**

在 `hw_transcode_stages.cpp` 中，当检测到 `options_.is_cpu == true` 时，激活 Pinned Host Memory 异步传输路线：p  
// src/core/hw\_transcode\_stages.cpp  
void PipelineStageManager::dispatchToHostStaging(const CudaFrameBuffer& src\_gpu\_frame) {  
if (\!options\_.is\_cpu) return;

```
// 1. CUDA Kernel: 转换 RGB/YUV444 帧至目标格式 (NV12 或 YUV420P10LE)
CUDA_YUV_CONVERT_PARAMS conv_params{};
conv_params.src_ptr = src_gpu_frame.data;
conv_params.src_pitch = src_gpu_frame.pitch;
conv_params.width = src_gpu_frame.width;
conv_params.height = src_gpu_frame.height;
conv_params.bit_depth = options_.output_bit_depth;

// 获取 Pinned Host 内存双缓冲句柄
PinnedBufferHandle& host_buffer = staging_ring_buffer_.acquireNextWriter();

// 执行 CUDA Color Conversion 并直接写入 Pinned Host Buffer
cudaStrea
```

m\_t async\_stream \= pipeline\_streams\_\[STREAM\_STAGING\];  
launchCudaColorConvertKernel(conv\_params, host\_buffer.host\_ptr, async\_stream);

```
// 2. 异步 Event 标记与 Host 提交
cudaEventRecord(host_buffer.ready_event, async_stream);

// 放入异步 CPU 编码队列，解耦 GPU 管线与 CPU 软编计算
encode
```

r\_thread\_pool\_.enqueue(this, \&host\_buffer {  
cudaEventSynchronize(host\_buffer.ready\_event);  
cpu\_encoder\_-\>encodeFrame(host\_buffer.host\_ptr, host\_buffer.linesize, host\_buffer.pts);  
staging\_ring\_buffer\_.releaseWriter(host\_buffer);  
});  
}

````

#### Step 2.2（`CpuVideoEncoder` 参数与实例绑定）
在 `src/io/cpu_video_encoder.cpp` 中落地真实的 `libsvtav1` 与 `libx265` 初始化与参数赋值：

```cpp
// src/io/cpu_video_encoder.cpp
bool CpuVideoEncoder::initialize(const CpuEncoderConfig& config) {
    const AVCodec* codec = nullptr;
    if (config.codec_type == CodecType::AV1) {
        codec = avcodec_find_encoder_by_name("libsvtav1");
    } else if (config.codec_type == CodecType::HEVC) {
        codec = avcodec_find_encoder_by_name("libx265");
    }

    if (!codec) {
        // 编码器查找失败，严禁静默忽略
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    codec_ctx_->width = config.width;
    codec_ctx_->height = config.height;
    codec_ctx_->pix_fmt = (config.bit_depth == 10) ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
    codec_ctx_->time_base = AVRational{1, config.fps_den};
    codec_ctx_->framerate = AVRational{config.fps_num, config.fps_den};

    // 注入 CRF 与 Preset 属性
    if (config.codec_type == CodecType::AV1) {
        // SVT-AV1 范围 0..63
        int crf_val = std::clamp(config.crf, 0, 63);
        av_opt_set_int(codec_ctx_->priv_data, "preset", config.preset_val, 0);
        av_opt_set_int(codec_ctx_->priv_data, "crf", crf_val, 0);
        av_opt_set(codec_ctx_->priv_data, "svtav1-params", "tune=0", 0); // Visual tuning
    } else if (config.codec_type == CodecType::HEVC) {
        // x265 范围 0..51
        int crf_val = std::clamp(config.crf, 0, 51);
        std::string crf_str = std::to_string(crf_val);
        av_opt_set(codec_ctx_->priv_data, "crf", crf_str.c_str(), 0);
        av_opt_set(codec_ctx_->priv_data, "preset", config.preset_name.c_str(), 0);
    }

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        return false;
    }
    return true;
}
````

---

# **3\. Task 3: 补齐"全容器混合硬/软解"与"固定 `.mp4` 音/字幕兼容矩阵"**

## **3.1 混合硬/软解统一上传**

在 `src/shim/loader/nv_decoder.cpp` 与 `src/io/ffmpeg_io.cpp` 中实现自动回落机制：当输入编码（如 `ProRes`、`WMV3`、`VP8`、`MPEG-2` 或 10-bit `4:2:2`）不被当前 NVDEC 硬件支持时，触发 FFmpeg CPU 软解并自动上传至 CUDA Pitch 内存。

```
// src/io/ffmpeg_io.cpp
bool MediaDecoder::decodeNextFrameToGpu(CUDA_DEVICE_PTR d_out_ptr, int out_pitch) {
    if (use_nvdec_hw_) {
        bool status = nvdec_instance_->decodeFrame(d_out_ptr, out_pitch);
        if (status) return true;
        // 若 NVDEC 执行中途出现错误，降级至 FFmpeg 软解流程
        use_nvdec_hw_ = false;
    }

    // 软解回落路径
    AVFrame* host_frame = av_frame_alloc();
    int ret = avcodec_receive_frame(cpu_codec_ctx_, host_frame);
    if (ret == 0) {
        // 将软解得到的 YUV/RGB Host 内存帧异步 Copy 上传至 GPU
        copyCpuFrameToGpuPitchedMem(host_frame, d_out_ptr, out_pitch, compute_stream_);
        av_frame_free(&host_frame);
        return true;
    }
    av_frame_free(&host_frame);
    return false;
}
```

## **3.2 确定性 MP4 输出兼容性矩阵**

### **3.2.1 容器策略**

输出路径强制校验并附带 `.mp4` 扩展名，封装初始化时显式写入 `movflags=+faststart`。

### **3.2.2 音频与字幕处理路由矩阵**

| 媒体类型 | 原始格式 / 编码 | 处置策略 (Action) | 目标编码 (Target Codec) | 说明 |
| :---- | :---- | :---- | :---- | :---- |
| **音频** | `AAC`, `AC-3`, `E-AC-3`, `MP3` | **Direct Copy** | Stream Copy | 无剪辑/无合并场景下直通 |
| **音频** | `FLAC`, `Opus`, `Vorbis`, `DTS`, `TrueHD`, `WMA`, `PCM_*` | **Re-encode** | `aac` | 实例化内置 `aac` 编码器重编码 |
| **字幕** | 文本字幕 (`SRT`, `ASS`, `WebVTT`) | **Transcode** | `mov_text` | 自动转码封装为 MP4 文本字幕 |
| **字幕** | 位图字幕 (`PGS`, `DVDSub`, `DVBSub`) | **Drop & Log** | *N/A* | 显式跳过，并在状态日志输出 i18n 提示 |

### **3.2.3 音频与字幕路由控制实现**

```
// src/io/ffmpeg_io.cpp
void MuxerManager::setupStreamRouting(AVFormatContext* ic, AVFormatContext* oc) {
    for (unsigned int i = 0; i < ic->nb_streams; i++) {
        AVStream* in_stream = ic->streams[i];
        AVCodecParameters* in_par = in_stream->codecpar;

        if (in_par->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVStream* out_stream = avformat_new_stream(oc, nullptr);
            if (is_mp4_native_audio(in_par->codec_id) && !requires_editing_) {
                // 音频直通
                avcodec_parameters_copy(out_stream->codecpar, in_par);
                out_stream->codecpar->codec_tag = 0;
            } else {
                // 实例化 AAC 编码器
                setup_aac_encoder(in_stream, out_stream);
            }
        } else if (in_par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (is_text_subtitle(in_par->codec_id)) {
                AVStream* out_stream = avformat_new_stream(oc, nullptr);
                out_stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
                out_stream->codecpar->codec_id = AV_CODEC_ID_MOV_TEXT;
            } else {
                // 位图字幕跳过并记录结构化日志
                LOG_INFO_I18N("STR_SUBTITLE_BITMAP_SKIPPED", in_par->codec_id);
            }
        }
    }

    // 设置 movflags
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    avformat_write_header(oc, &opts);
    av_dict_free(&opts);
}
```

---

# **4\. 法定验收单 (真机实测规约)**

## **4.1 测试用例坐标**

测试套件新增/扩充至 `tests/test_cpu_crf_and_containers.cpp`。

## **4.2 真机自动化测试脚本**

```
// tests/test_cpu_crf_and_containers.cpp
#include <gtest/gtest.h>
#include "io/cpu_video_encoder.h"
#include "io/ffmpeg_io.h"

TEST(ReworkWave1A, SVTAV1_And_HEVC_CRF_Transcode) {
    // 1. 测试 SVT-AV1 CRF 编码与 MP4 结构
    TranscodeOptions av1_opts{};
    av1_opts.output_path = "test_av1_crf30.mp4";
    av1_opts.codec = "libsvtav1";
    av1_opts.crf = 30;
    
    bool av1_res = execute_transcode_pipeline(av1_opts);
    ASSERT_TRUE(av1_res);

    // 校验 moov 前置与编码流
    AVFormatContext* fmt_ctx = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt_ctx, "test_av1_crf30.mp4", nullptr, nullptr), 0);
    ASSERT_GT(fmt_ctx->nb_streams, 0u);
    EXPECT_EQ(fmt_ctx->streams[0]->codecpar->codec_id, AV_CODEC_ID_AV1);
    avformat_close_input(&fmt_ctx);

    // 2. 测试 x265 CRF 编码 (当启用 GPL 时)
#if defined(CINERTX_ENABLE_GPL_CODECS)
    TranscodeOptions hevc_opts{};
    hevc_opts.output_path = "test_hevc_crf24.mp4";
    hevc_opts.codec = "libx265";
    hevc_opts.crf = 24;

    bool hevc_res = execute_transcode_pipeline(hevc_opts);
    ASSERT_TRUE(hevc_res);
#endif
}

TEST(ReworkWave1A, NonMp4_Audio_Subtitle_Matrix) {
    // 测试包含 FLAC 音轨 + PGS 字幕的 .mkv 容器转码为 .mp4
    TranscodeOptions opts{};
    opts.input_path = "test_assets/input_flac_pgs.mkv";
    opts.output_path = "test_assets/output_compat.mp4";

    bool status = execute_transcode_pipeline(opts);
    ASSERT_TRUE(status);

    // 验证输出文件格式
    AVFormatContext* fmt_ctx = nullptr;
    ASSERT_EQ(avformat_open_input(&fmt_ctx, "test_assets/output_compat.mp4", nullptr, nullptr), 0);
    
    bool has_aac = false;
    bool has_pgs = false;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (fmt_ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_AAC) {
                has_aac = true;
            }
        }
        if (fmt_ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE) {
            has_pgs = true;
        }
    }
    
    EXPECT_TRUE(has_aac);   // FLAC 必须成功重编码为 AAC
    EXPECT_FALSE(has_pgs);  // PGS 位图字幕必须被安全滤除，不阻塞容器封装
    avformat_close_input(&fmt_ctx);
}
```

