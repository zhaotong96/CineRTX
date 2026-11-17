# CineRTX Phase 10 施工单（Track 10B）：全模态编码引擎（NVENC `CQ` / `QP` 与纯 CPU `CRF`）

- **文档编号**：`CINERTX-P10-TRACK-10B`
- **对应总纲任务**：任务 3（编码要 `CQ`、`QP` 还有纯 CPU 的 `CRF` 模式）
- **核心目标**：补齐专业母版压制与硬件高效编码的全模态矩阵——实现开启 Spatial AQ (`-spatial-aq 1`) 的 NVENC **`CQ`（Constant Quality，设为全局默认）**、带 I/P/B 量化步长偏置的 **`QP`（ConstQP）**，以及基于 `libsvtav1` / `libx265` 10-bit 静态集成的纯 CPU **`CRF`（Constant Rate Factor）** 管线。

---

## 一、 编码码率控制现状剖析与架构演进对比

| 码控模式 | 枚举值 (`rate_control`) | 适用后端 | 底层机制与数学定义 | 现网 `v0.2.0` 缺陷 | Phase 10 (`v0.3.0`) 精确实现 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`CQ`** *(新默认)* | `"cq"` (`RTX_RC_CQ = 3`) | NVENC (`av1` / `hevc` / `h264`) | `NV_ENC_PARAMS_RC_VBR` + `targetQuality = cq (1..51)` + `averageBitRate = 0` + `enableAQ = 1 (aqStrength = 8)` | **完全缺失**（`nv_encoder.cpp:291` 甚至硬编码 `enableAQ = 0`，默认用画质最差的 `cbr`） | 启用 NVENC 原生感知恒定质量模式，配合 Spatial AQ 在暗部/平坦区自适应分配比特。 |
| **`QP`** | `"qp"` (`RTX_RC_CONSTQP = 2`) | NVENC (`av1` / `hevc` / `h264`) | `NV_ENC_PARAMS_RC_CONSTQP`，对每个宏块/CTU 施加固定量化参数 | `qpIntra = qpInterP = qpInterB = qp` 无差别一刀切，导致 I 帧呼吸闪烁、B 帧体积暴涨 | 引入标准阶梯偏置：`qpIntra = max(0, qp - 2)`，`qpInterP = qp`，`qpInterB = min(51, qp + 2)`。 |
| **`CRF`** | `"crf"` (`RTX_RC_CRF = 4`) | CPU 软编 (`av1_cpu` / `hevc_cpu` / `h264_cpu`) | 基于人眼心理视觉模型与运动复杂度树的恒定速率因子（`libsvtav1` / `libx265` / `libx264` CRF `0..51`） | **完全缺失**（FFmpeg 静态编译禁用了全部视频编码器，无 GPU->CPU P010 异步回传与软编通道） | 静态编译 `libsvtav1` + `libx265`，通过双缓冲 Pinned Memory (`cuMemAllocHost`) 异步 DtoH 喂给 `CpuVideoEncoder`。 |
| **`VBR` / `CBR`** | `"vbr"` (`1`) / `"cbr"` (`0`) | NVENC & CPU | 受目标平均码率 `bitrate_kbps` 约束的可变/恒定比特率 | 未开启 Spatial AQ | 默认开启 `enableAQ = 1`，保留用于流媒体定码率场景。 |

---

## 二、 施工细则 1：升级 `include/shim/rtx_shim_abi.h` 与 `src/shim/loader/nv_encoder.cpp`

### 2.1 扩展 `RtxRateControl` 枚举与配置结构体 (`include/shim/rtx_shim_abi.h`)
```cpp
enum RtxRateControl : int32_t {
    RTX_RC_CBR     = 0,
    RTX_RC_VBR     = 1,
    RTX_RC_CONSTQP = 2,
    RTX_RC_CQ      = 3,  // NVENC VBR + targetQuality + Spatial AQ (Phase 10 Default)
    RTX_RC_CRF     = 4,  // CPU Software Encoder Constant Rate Factor
};

struct RtxEncoderConfig {
    int32_t codec;          // 0=HEVC, 1=AV1, 2=H264
    int32_t width;
    int32_t height;
    int32_t fps_num;
    int32_t fps_den;
    int32_t bitrate_kbps;   // 0 = auto / uncapped in CQ mode
    int32_t preset;         // 1..7 (NVENC P1..P7) or 0..13 (CPU preset)
    int32_t tier;
    int32_t gop_length;
    int32_t input_format;   // RTX_PIXEL_NV12 or RTX_PIXEL_P010
    int32_t rate_control;   // RtxRateControl (0..4)
    int32_t qp;             // Unified quality parameter (0..51 for QP/CQ/CRF, default 22)
    int32_t spatial_aq;     // 1 = enable Spatial AQ (default 1), 0 = disable
    int32_t aq_strength;    // 1..15 (default 8)
    int32_t device_index;
    RtxHdrMetadata hdr;
};
```

### 2.2 `src/shim/loader/nv_encoder.cpp` 中 `CQ` 与 `QP` 精确实现
将 `nv_encoder.cpp:267-295` 替换为以下 C++26 良构逻辑，并支持切镜强制 `IDR` 帧（配合 Track 10C）：

```cpp
NV_ENC_RC_PARAMS& rc = out->rcParams;
const int32_t q_val = std::clamp(config.qp > 0 ? config.qp : 22, 0, 51);

switch (config.rate_control) {
    case RTX_RC_CQ: {
        rc.rateControlMode = NV_ENC_PARAMS_RC_VBR;
        rc.targetQuality = static_cast<uint8_t>(std::clamp(q_val, 1, 51));
        rc.targetQualityLSB = 0;
        rc.averageBitRate = 0u;  // Pure Constant Quality without artificial average floor
        const uint32_t cap_bps = config.bitrate_kbps > 0
            ? static_cast<uint32_t>(config.bitrate_kbps) * 1000u
            : 120000000u;        // 120 Mbps safety ceiling for 4K120 HDR10
        rc.maxBitRate = cap_bps;
        rc.vbvBufferSize = cap_bps;
        break;
    }
    case RTX_RC_CONSTQP: {
        rc.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
        rc.constQP.qpIntra  = static_cast<uint32_t>(std::max(0, q_val - 2));
        rc.constQP.qpInterP = static_cast<uint32_t>(q_val);
        rc.constQP.qpInterB = static_cast<uint32_t>(std::min(51, q_val + 2));
        break;
    }
    case RTX_RC_VBR: {
        rc.rateControlMode = NV_ENC_PARAMS_RC_VBR;
        rc.averageBitRate = bitrate;
        rc.maxBitRate = static_cast<uint32_t>(bitrate * 1.5);
        rc.vbvBufferSize = bitrate * 2u;
        break;
    }
    case RTX_RC_CBR:
    default: {
        rc.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        rc.averageBitRate = bitrate;
        rc.maxBitRate = bitrate;
        rc.vbvBufferSize = bitrate * 2u;
        break;
    }
}

// Enable NVIDIA Spatial Adaptive Quantization (-spatial-aq 1) by default for CQ/VBR/CBR
if (config.rate_control != RTX_RC_CONSTQP && config.spatial_aq != 0) {
    rc.enableAQ = 1u;
    rc.aqStrength = static_cast<uint32_t>(std::clamp(config.aq_strength > 0 ? config.aq_strength : 8, 1, 15));
} else {
    rc.enableAQ = 0u;
}
rc.enableTemporalAQ = 0u;
rc.enableLookahead = 0u;
rc.zeroReorderDelay = 1u;
```

同时在 `rtx_shim_encoder_encode` 中支持当 `frame->keyframe != 0`（由切镜检测器 `ShotGroupSegmenter` 触发）时置位 `pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS`！

---

## 三、 施工细则 2：纯 CPU `CRF` 10-Bit HDR10 软编码管线 (`CpuVideoEncoder`)

### 3.1 `cmake/RtxFfmpegStatic.cmake` 静态编译集成
1. 在 `cmake/RtxFfmpegStatic.cmake` 中，将 `SVT-AV1`（`libsvtav1`）与 `x265`（`libx265` 10-bit）纳入 `.vs/build/external/ffmpeg-static/` 自动化静态构建缓存，并在 FFmpeg `configure` 的 `--enable-encoder=` 列表中加入 `libsvtav1,libx265,libx264`。
2. 保持与现有 FFmpeg 静态归档完全一致的缓存命中早退机制：一旦 `.vs/build/external/ffmpeg-static/<toolchain>/lib/libavcodec.a` 包含 `ff_libsvtav1_encoder` 与 `ff_libx265_encoder` 符号，后续 `cmake` Configure 0 秒瞬时跳过。

### 3.2 新增 `include/rtx/cpu_video_encoder.h` 与 `src/io/cpu_video_encoder.cpp`
当 `TranscodeProfile` 指定 `rate_control == "crf"` 或 `codec` 为 `"av1_cpu"` / `"hevc_cpu"` 时，管线启用 `rtx::CpuVideoEncoder`：

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "rtx/io.h"
#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

struct CpuEncoderConfig {
    VideoCodec codec = VideoCodec::AV1;  // AV1 (libsvtav1) | HEVC (libx265) | H264 (libx264)
    int width = 3840;
    int height = 2160;
    Rational frame_rate{60, 1};
    int crf = 22;                        // 0..51 (SVT-AV1 / x265 / x264 CRF)
    int preset = 5;                      // SVT-AV1 preset (1..12) or x265 preset (0..9)
    int threads = 0;                     // 0 = auto (all logical cores on Ryzen 9 8945HX)
    Hdr10Metadata hdr{};
};

class CpuVideoEncoder {
public:
    CpuVideoEncoder() = default;
    ~CpuVideoEncoder() noexcept;

    CpuVideoEncoder(const CpuVideoEncoder&) = delete;
    CpuVideoEncoder& operator=(const CpuVideoEncoder&) = delete;
    CpuVideoEncoder(CpuVideoEncoder&&) noexcept;
    CpuVideoEncoder& operator=(CpuVideoEncoder&&) noexcept;

    [[nodiscard]] static Result<CpuVideoEncoder> create(const CpuEncoderConfig& cfg);

    // Accepts a host-staged P010 frame (double-buffered via pinned memory from GPU P010 ring
    // or directly from software decode), converts/maps to AV_PIX_FMT_P010LE / YUV420P10LE,
    // attaches HDR10 / HDR10+ side data, and drains encoded packets.
    [[nodiscard]] Result<std::vector<Packet>> encodeP010Frame(
        const uint16_t* y_plane, size_t y_pitch_bytes,
        const uint16_t* uv_plane, size_t uv_pitch_bytes,
        int64_t pts_90k, bool force_keyframe,
        std::span<const uint8_t> hdr10plus_payload = {});

    [[nodiscard]] Result<std::vector<Packet>> flush();
    [[nodiscard]] std::span<const uint8_t> extradata() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtx
```

#### AVX2 / YMM 向量化 `P010 -> YUV420P10LE` 解交织核（适配 `libx265` / `libsvtav1`）
- `libsvtav1` 与 `libx265` 接收平面格式 `AV_PIX_FMT_YUV420P10LE`（10-bit 居右 `0..1023`，U/V 平面分离），而 GPU 产出的 `P010` 是 10-bit 居左（高 10 位有效，`val >> 6`）且 UV 交织。
- 在 `src/io/cpu_video_encoder.cpp` 中使用 `<immintrin.h>` **AVX2 `YMM` 256-bit 向量指令**实现单遍拷贝：
  - Luma 行：`_mm256_srli_epi16(_mm256_loadu_si256(src_y), 6)` 直接右移 6 位写入 `AVFrame->data[0]`；
  - Chroma 行：`_mm256_srli_epi16` + `_mm256_shuffle_epi8` / `_mm256_permutevar8x32_epi32` 单指令流解交织 U/V 并右移 6 位分别写入 `AVFrame->data[1]` 与 `AVFrame->data[2]`。

---

## 四、 Track 10B 独立验证门禁（Checklist）

- [ ] **单测门禁 (`tests/test_io.cpp`)**：
  - 新增 `test_nvenc_cq_and_qp_config`：验证 `rate_control = "cq"` 时 `NV_ENC_RC_PARAMS::rateControlMode == NV_ENC_PARAMS_RC_VBR`、`targetQuality == cq`、`enableAQ == 1`，且 `rate_control = "qp"` 时 `qpIntra == qp - 2`、`qpInterB == qp + 2`。
  - 新增 `test_cpu_crf_encoder_10bit_hdr10`：使用合成 10-bit `P010` 渐变测试序列分别喂入 `CpuVideoEncoder` (`AV1` `crf=20` vs `crf=32` 及 `HEVC` `crf=22`)，断言产出合法的 OBU/HEVC 码流、HDR10 `MasteringDisplay` 侧数据完整，且 `crf=20` 码流字节数 $> 1.8 \times \text{bytes}(\text{crf}=32)$。
