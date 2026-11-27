# **25\_target\_wave1-A\_静态SVT-AV1与MP4兼容矩阵真接线施工单**

Superpowers 原子靶向施工规约 — 纠偏 23 号驳回裁决：Wave 1-B / 1-A 静态软编接入与死代码拔除

## **1\. 背景、前置依赖与宪法红线**

### **1.1 前置依赖验证**

* **门禁状态**：必须基于 `24_target_wave0-A` 宿主编译与四重门禁全绿状态进行（全构建通过、静态分析无违规）。  
* **执行人/日期**：Person \| Date

### **1.2 驳回靶点分析（针对 23 号裁决）**

在第一轮交付中，存在"伪接入"现象：仅对 `param_schema.cpp` 与 `cpu_video_encoder.cpp` 增加了参数枚举，但底层的构建系统与 Muxer 架构存在严重硬伤：

* **CMake 虚假依赖**：`cmake/RtxFfmpegStatic.cmake` 未真正引入与编译静态 `libsvtav1`（以及 GPL 预设下需隔离引入的 `libx265`），导致 `avcodec_find_encoder_by_name("libsvtav1")` 运行时空指针返回。  
* **死代码与占位符**：`src/io/ffmpeg_io.cpp:59-81` 中的 `isMp4DirectCopyAudioCodec()` 与 `isMp4TextSubtitleCodec()` 被标记为 `[[maybe_unused]]` 悬空挂载，MP4 容器写入未真正经过格式兼容判定逻辑，导致非标准音轨或字幕直接打穿写 Header 阶段。

### **1.3 宪法级工程红线**

1. **类型数量严格管控**：`AuditTypes <= 34`（Headless）/ `35`（GUI），**零新增**具名 `struct` / `class`。  
2. **动态导入 DLL 严控**：`AuditImports` GUI 模式 `<= 4`；HEADLESS 模式 `== 2`。不可因为引入静态库导致系统动态依赖暴涨。  
3. **零死函数红线**：根据 `[DEC-016]` 规则，严格清理所有 `[[maybe_unused]]` 函数，函数必须真实接入数据流管线并承担控制决策。  
4. **封装目标隔离**：输出容器强制固定为 `.mp4`，且必须自动启用 `+faststart` 标记（Moov Box 前置）。

---

## **2\. 靶向 Task 1：在 `RtxFfmpegStatic.cmake` 实装静态 `libsvtav1` 与 `libx265` 编联**

### **2.1 构建体系改造规格**

在 `cmake/RtxFfmpegStatic.cmake` 中追加并实装 `SvtAv1Enc` 与 `libx265` 的 CMake ExternalProject / FetchContent 依赖编联，确保完全静态链接入 FFmpeg 编译链。

* **SvtAv1Enc（BSD 许可证）**：  
  * 强制参数：`-DBUILD_SHARED_LIBS=OFF -DENABLE_AVX2=ON`。  
  * CRT 运行时对齐：MSVC 环境下强制与主程序一致（`/MT` 或 `/MD`）。  
  * FFmpeg 显式配置：在 FFmpeg `configure` 步骤中显式添加 `--enable-libsvtav1`。  
* **libx265（GPL 许可证，受开关隔离）**：  
  * 条件编译：仅当 `CINERTX_ENABLE_GPL_CODECS=ON` 时触发编译。  
  * 强制参数：`-DENABLE_SHARED=OFF -DENABLE_CLI=OFF`。  
  * FFmpeg 显式配置：FFmpeg `configure` 步骤中补充 `--enable-libx265` 与 `--enable-gpl`。  
* **基准静态 Configure 选项保持**：

```
--disable-network --disable-schannel --disable-protocols \
--enable-protocol=file,pipe --disable-autodetect --disable-programs \
--disable-shared --enable-static
```

### **2.2 静态库与系统导入库链入控制**

为确保 `AuditImports` 在 Windows 平台上不超标，必须对 Linker 导入表进行精细化修剪：

| 构建目标 | 静态库依赖项 | 系统导入库 (System Link Libraries) | AuditImports 监控阈值 |
| :---- | :---- | :---- | :---- |
| **SvtAv1Enc** | `SvtAv1Enc.lib` | `kernel32.lib`, `user32.lib` | GUI \<= 4 DLLs |
| **libx265** (GPL) | `x265.lib` | `kernel32.lib` | GUI \<= 4 DLLs |
| **FFmpeg Muxer Core** | `libavcodec.a`, `libavformat.a`, `libavutil.a` | `bcrypt.lib`, `strmiode.lib` | HEADLESS \== 2 DLLs |

---

## **3\. 靶向 Task 2：拔除 `ffmpeg_io.cpp` 死代码并真实接入 Muxer 兼容矩阵**

### **3.1 剥离 `[[maybe_unused]]` 挂载**

彻底移除 `src/io/ffmpeg_io.cpp:59-81` 处的 `[[maybe_unused]]` 属性修饰，将 `isMp4DirectCopyAudioCodec()` 与 `isMp4TextSubtitleCodec()` 函数声明与实现全面置于调用链的必经路径上。

### **3.2 容器封装兼容性决策矩阵**

在 `FfmpegMuxer` 结构初始化流（`src/io/ffmpeg_io.cpp` 中的 `initialize_streams()`）时，对输入音视频与字幕流实施自动校验与转码分流：

```
                              [ 输入媒体流 Pipeline ]
                                         │
               ┌─────────────────────────┼─────────────────────────┐
               ▼                         ▼                         ▼
         【 视频流 】                【 音频流 】                【 字幕流 】
               │                         │                         │
     软硬解回落至 GPU/CPU 帧       是否支持 Direct Copy?         文本字幕 vs 位图字幕
               │                   (AAC / Opus等+无剪辑)            │
               │                         │                         ├─ 文本(SRT/ASS/VTT)
               │                 ┌───────┴───────┐                 │  └─► 转换为 mov_text
               │                 ▼               ▼                 │
               │              [ 直通 ]      [ 软重编码 ]           └─ 实时位图(PGS/DVDSub)
               │           (Stream Copy)    (内置 aac)                └─► 自动跳过并记录 Log
               │                         │                            (不打断 Muxer Header)
               └─────────────────────────┼─────────────────────────┘
                                         ▼
                             [ FFmpeg MP4 Muxer ]
                              (+faststart Moov前置)
```

### **3.3 MP4 封装兼容判定与动作控制表**

| 流类型 | 输入源编码 (Input Codec) | 兼容判定逻辑 | 决策动作 (Muxer Action) | 异常/兜底机制 |
| :---- | :---- | :---- | :---- | :---- |
| **音频** | AAC, MP3 | `isMp4DirectCopyAudioCodec() == true` 且无剪辑 | **Stream Copy** (直通写入) | 无须重编码，直接写 Packets |
| **音频** | FLAC, Opus, Vorbis, PCM, DTS, TrueHD, WMA | `isMp4DirectCopyAudioCodec() == false` 或存在音轨重采样 | **软重编码** 自动实例化内置 `aac` 编码器 | 采样率超标时自动降采样至 48kHz |
| **字幕** | SRT, ASS, WebVTT | `isMp4TextSubtitleCodec() == true` | **转封装 (Transcode)** 为 `mov_text` | 滤除 ASS 复杂特效样式标记 |
| **字幕** | PGS (HDMV), DVDSub, DVBSub | `isMp4TextSubtitleCodec() == false` | **跳过该流 (Skip)** | 输出 Log 并顺畅写入 Header |

---

## **4\. 靶向 Task 3：贯通 `GpuToCpuStaging` \-\> `CpuVideoEncoder` 与混合解码回落**

### **4.1 SVT-AV1 CRF 编码与上下文配置**

在 `CpuVideoEncoder` 中引入基于 `libsvtav1` 的 CRF（Constant Rate Factor）控制逻辑，严格约束参数映射区间：

* **Encoder 检索校验**：显式执行 `avcodec_find_encoder_by_name("libsvtav1")`，校验返回值非 `nullptr`。  
* **CRF 参数绑定**：设置 `AVCodecContext::priv_data` 中的 `crf` 选项（取值范围 `0..63`，默认推荐值 `30`）。  
* **帧拷贝路径**：通过 `GpuToCpuStaging` 机制完成 Cuda Memory/DirectX 纹理到 Host 内存 Pinned Buffer 的异步 Readback，而后送入 `CpuVideoEncoder`。

### **4.2 混合硬/软解回落策略（Hybrid Decoder Fallback）**

对于 NVIDIA NVDEC 无法原生硬件加速解压的格式（如 ProRes, WMV3, VP8, MPEG-2 等），构建降级解包机制：

```
// 解码器初始化路由策略伪代码机制
AVCodecID input_codec_id = demux_ctx.get_codec_id();
if (nv_decoder_supports(input_codec_id)) {
    init_nvdec_decoder(); // GPU 硬件解码路径
} else {
    init_ffmpeg_software_decoder(); // 降级至 CPU 软解路径
    // 软解输出 AVFrame (YUV420P / YUV422P / RGBA)
    // 统一通过 FrameUploader 异步 Upload 至 GPU 内存作为统一帧格式
    upload_to_unified_gpu_surface();
}
```

---

## **5\. 宿主机验收门禁与测试用例矩阵**

为保障施工质量，构建完成后必须在宿主机上通过以下自动化命令与验收测试：

### **5.1 门禁测试用例矩阵**

| 用例编号 | 输入文件与格式 | 目标参数与编码器 | 预期行为与判定指标 |
| :---- | :---- | :---- | :---- |
| **TC-W1A-01** | `input_4k.y4m` | `av1_cpu`, `crf=30` | 1\. 成功生成 MP4；2. `ffprobe` 确认视频编码为 `av1`；3. 检查 Moov box 位于文件头部 (`+faststart`)。 |
| **TC-W1A-02** | `input_4k.y4m` | `hevc_cpu`, `crf=24` (GPL) | 1\. 成功生成 MP4；2. 验证 `x265` 静态库调用正常；3. 无 DLL 动态加载冲突。 |
| **TC-W1A-03** | `sample_flac_pgs.mkv(包含 FLAC 音轨 + PGS 字幕)` | 默认 MP4 输出配置 | 1\. 音轨自动重编码为 `aac`；2. PGS 字幕被忽略，提示 Warning 结构化日志；3. `avformat_write_header` 成功执行无崩溃。 |
| **TC-W1A-04** | `sample_legacy.wmv(WMV3 视频 + WMA 音频)` | 目标 MP4 (AV1+AAC) | 1\. WMV3 软解回落生效并上传至 GPU Surface；2. WMA 自动转码 AAC；3. 音视频同步无丢帧。 |
| **TC-W1A-05** | 编译期与二进制检查 | `AuditTypes` & `AuditImports` | 1\. 静态分析报告中 `[[maybe_unused]]` 数量为 0；2. GUI 模式动态导入 DLL 数量 `<= 4`。 |

### **5.2 宿主机端到端实测指令集**

```
# 1. 验证静态库与编解码器发现
./bin/cinertx_headless --list-encoders | grep -E "libsvtav1|libx265"

# 2. 端到端 AV1 CPU 编码与 faststart 校验
./bin/cinertx_headless -i input.mp4 -c:v libsvtav1 -crf 30 -preset 8 -o output_av1.mp4
ffprobe -v error -show_entries format=format_name:stream=codec_name output_av1.mp4
qtfaststart -c output_av1.mp4

# 3. 复杂容器兼容性（FLAC + PGS 转 MP4）实测
./bin/cinertx_headless -i sample_flac_pgs.mkv -c:v libsvtav1 -o output_complex.mp4
ffprobe -v error -show_entries stream=codec_name output_complex.mp4

# 4. 静态审计与死代码检视
ninja audit_symbols
```

---

## **6\. 施工确认与签署**

- [x] `cmake/RtxFfmpegStatic.cmake` 已实装 `libsvtav1` 与 `libx265` 静态构建与 FFmpeg 显式使能。  
- [x] `src/io/ffmpeg_io.cpp:59-81` 彻底清理 `[[maybe_unused]]`，音频与字幕兼容性决策矩阵真实接入 Muxer 流。  
- [x] 混合硬/软解回落与 GPU Surface 统一上传测试通过。  
- [x] 宿主机四重门禁全绿，`AuditTypes` 与 `AuditImports` 零超标。

**施工负责人**：Person \| **复核专家**：Person \| **签署日期**：Date