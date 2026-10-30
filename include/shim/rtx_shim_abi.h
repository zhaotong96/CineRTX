/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Pure C ABI between the GPLv3 core engine and the proprietary NVIDIA runtime.
 * Nothing in this header may include an NVIDIA SDK header.  Handles are opaque
 * and all device pointers are 64-bit integers so that the core never needs to
 * know whether CUDA is present at compile time.
 */

#ifndef RTX_SHIM_ABI_H
#define RTX_SHIM_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#if defined(RTX_SHIM_EXPORTS)
#define RTX_SHIM_API __declspec(dllexport)
#elif defined(RTX_SHIM_STATIC)
#define RTX_SHIM_API
#else
#define RTX_SHIM_API __declspec(dllimport)
#endif
#define RTX_SHIM_CALL __cdecl
#else
#define RTX_SHIM_API __attribute__((visibility("default")))
#define RTX_SHIM_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RTX_SHIM_ABI_VERSION 2u
#define RTX_MAX_ERROR_MESSAGE 512
#define RTX_MAX_DEVICE_NAME 128
#define RTX_MAX_COMPONENTS 16

typedef enum RtxResult {
    RTX_OK = 0,
    RTX_ERR_INVALID_ARGUMENT = 1,
    RTX_ERR_NOT_FOUND = 2,
    RTX_ERR_IO = 3,
    RTX_ERR_UNSUPPORTED = 4,
    RTX_ERR_DLL_MISSING = 5,
    RTX_ERR_ARCH_UNSUPPORTED = 6,
    RTX_ERR_DRIVER_TOO_OLD = 7,
    RTX_ERR_DEVICE_LOST = 8,
    RTX_ERR_OUT_OF_MEMORY = 9,
    RTX_ERR_NGX = 10,
    RTX_ERR_TENSORRT_VERSION = 11,
    RTX_ERR_NOT_IMPLEMENTED = 12,
    RTX_ERR_INTERNAL = 13
} RtxResult;

typedef enum RtxPixelFormat {
    RTX_PIXEL_UNKNOWN = 0,
    RTX_PIXEL_NV12 = 1,
    RTX_PIXEL_P010 = 2,
    RTX_PIXEL_RGB32 = 3,
    RTX_PIXEL_R10G10B10A2 = 4,
    RTX_PIXEL_RGBA16F = 5
} RtxPixelFormat;

typedef enum RtxVideoCodec {
    RTX_CODEC_UNKNOWN = 0,
    RTX_CODEC_H264 = 1,
    RTX_CODEC_HEVC = 2,
    RTX_CODEC_AV1 = 3,
    RTX_CODEC_VP9 = 4
} RtxVideoCodec;

typedef enum RtxCapabilityBits {
    RTX_CAP_NVDEC = 1u << 0,
    RTX_CAP_NVENC_HEVC10 = 1u << 1,
    RTX_CAP_NVENC_AV1 = 1u << 2,
    RTX_CAP_VSR = 1u << 3,
    RTX_CAP_TRUEHDR = 1u << 4,
    RTX_CAP_NVOFA = 1u << 5,
    RTX_CAP_TENSORRT = 1u << 6,
    RTX_CAP_D3D12_INTEROP = 1u << 7,
    RTX_CAP_CPU_REFERENCE = 1u << 8
} RtxCapabilityBits;

typedef struct RtxDeviceInfo {
    char name[RTX_MAX_DEVICE_NAME];
    uint8_t luid[8];
    uint32_t luid_node_mask;
    int32_t compute_major;
    int32_t compute_minor;
    uint64_t vram_bytes;
    uint32_t driver_version;
    uint32_t cuda_version;
    uint32_t capabilities;
    uint8_t reserved[60];
} RtxDeviceInfo;

typedef struct RtxComponentInfo {
    char name[64];
    char path[260];
    int32_t present;
    int32_t version;
    char note[128];
} RtxComponentInfo;

typedef struct RtxDiagnostics {
    RtxDeviceInfo device;
    uint32_t component_count;
    RtxComponentInfo components[RTX_MAX_COMPONENTS];
    char summary[RTX_MAX_ERROR_MESSAGE];
} RtxDiagnostics;

typedef struct RtxFrameDesc {
    uint64_t planes[3];
    uint32_t pitches[3];
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t bit_depth;
    int64_t pts;
    uint64_t fence_value;
    /* Identifies the shim-owned VRAM slot.  Pass it to
     * rtx_shim_decoder_release_frame() once the encoder has consumed the frame. */
    uint64_t slot_index;
} RtxFrameDesc;

typedef struct RtxHdrMetadata {
    int32_t enabled;
    uint16_t red_x, red_y, green_x, green_y, blue_x, blue_y, white_x, white_y;
    uint32_t max_luminance, min_luminance;
    uint16_t max_cll, max_fall;
    int32_t primaries, transfer, matrix, range;
    uint32_t target_nits;
} RtxHdrMetadata;

typedef struct RtxEncoderConfig {
    int32_t codec; /* 0 = HEVC, 1 = AV1, 2 = H.264 */
    int32_t width, height;
    int32_t fps_num, fps_den;
    int32_t bitrate_kbps;
    int32_t preset; /* p1..p7 */
    int32_t profile; /* main10 */
    int32_t level;
    int32_t tier;
    RtxHdrMetadata hdr;
    int32_t input_format; /* RtxPixelFormat: RTX_PIXEL_NV12 or RTX_PIXEL_P010 */
    int32_t gop_length;   /* 0 = encoder default */
    int32_t rate_control; /* 0 = CBR, 1 = VBR, 2 = const-QP */
    int32_t qp;           /* used when rate_control == 2 */
} RtxEncoderConfig;

typedef struct RtxDecoderConfig {
    int32_t codec;       /* RtxVideoCodec */
    int32_t width;
    int32_t height;
    int32_t bit_depth;   /* 8 or 10 */
    int32_t output_format; /* RTX_PIXEL_NV12 or RTX_PIXEL_P010 */
    int32_t clock_rate;  /* timestamps per second; 0 = 90 kHz */
    int32_t max_surfaces;/* 0 = auto */
    int32_t pool_slots;  /* 0 = 8 */
} RtxDecoderConfig;

typedef struct RtxHwStats {
    uint64_t frames_in;
    uint64_t frames_out;
    uint64_t device_to_device_bytes;
    uint64_t host_to_device_pixel_bytes;
    uint64_t device_to_host_pixel_bytes;
    uint64_t host_pixel_copies;
    uint64_t bitstream_bytes;
} RtxHwStats;

typedef struct RtxPacketDesc {
    const uint8_t* data;
    size_t size;
    int64_t pts;
    int64_t dts;
    int64_t duration;
    int32_t keyframe;
    int32_t reserved;
} RtxPacketDesc;

typedef struct RtxTrueHdrConfig {
    int32_t contrast;
    int32_t saturation;
    int32_t middle_gray;
    int32_t max_luminance;
} RtxTrueHdrConfig;

typedef struct RtxBackendConfig {
    int32_t width;
    int32_t height;
    int32_t target_width;
    int32_t target_height;
    int32_t vsr_quality;
    int32_t enable_truehdr;
    int32_t enable_nvofa;
    int32_t enable_rife;
    uint64_t vram_budget_bytes;
    RtxTrueHdrConfig truehdr;
} RtxBackendConfig;

typedef struct RtxBackend RtxBackend;
typedef struct RtxDecoder RtxDecoder;
typedef struct RtxEncoder RtxEncoder;

typedef struct RtxBackendVTable {
    RtxResult(RTX_SHIM_CALL* probe)(RtxDiagnostics* diagnostics);
    RtxResult(RTX_SHIM_CALL* create)(const RtxBackendConfig* config, RtxBackend** backend);
    void(RTX_SHIM_CALL* destroy)(RtxBackend* backend);
    RtxResult(RTX_SHIM_CALL* get_device_info)(RtxBackend* backend, RtxDeviceInfo* info);
    RtxResult(RTX_SHIM_CALL* get_capabilities)(RtxBackend* backend, uint32_t* capabilities);
    const char*(RTX_SHIM_CALL* last_error)(RtxBackend* backend);
} RtxBackendVTable;

/* Probe the local NVIDIA stack without creating a CUDA context. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_probe(RtxDiagnostics* diagnostics);

/* Create an opaque NVIDIA backend.  Returns RTX_ERR_ARCH_UNSUPPORTED when the
 * requested features require Turing (7.5) or newer and the device is older. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_create_backend(const RtxBackendConfig* config,
                                                             RtxBackend** backend);
RTX_SHIM_API void RTX_SHIM_CALL rtx_shim_destroy_backend(RtxBackend* backend);
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_get_device_info(RtxBackend* backend,
                                                              RtxDeviceInfo* info);
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_get_capabilities(RtxBackend* backend,
                                                               uint32_t* capabilities);
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_last_error(RtxBackend* backend);
RTX_SHIM_API uint32_t RTX_SHIM_CALL rtx_shim_abi_version(void);

/* -------------------------------------------------------------------------
 * Stage 1 hardware codec ABI (NVDEC + NVENC).
 *
 * Decoded frames live exclusively in shim-owned device memory.  The shim maps
 * the CUVID surface, copies it device-to-device into a pre-allocated slot with
 * cuMemcpy2DAsync, and unmaps the CUVID surface immediately.  No pixel byte is
 * ever transferred to host memory by this API.
 * ---------------------------------------------------------------------- */

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_create(const RtxDecoderConfig* config, RtxDecoder** decoder);
RTX_SHIM_API void RTX_SHIM_CALL rtx_shim_decoder_destroy(RtxDecoder* decoder);

/* Submits one Annex-B access unit.  The parser may synchronously invoke the
 * display callback and queue zero or more frames. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_submit(RtxDecoder* decoder, const uint8_t* data, size_t size,
                        int64_t pts, int64_t dts, int32_t flags);

/* Pops the next decoded frame.  Returns RTX_ERR_NOT_FOUND when the parser has
 * not produced a frame yet and RTX_ERR_IO after the end-of-stream was
 * submitted and all frames were drained. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_receive(RtxDecoder* decoder, RtxFrameDesc* frame);

/* Signals end of stream to the CUVID parser and drains display-order frames. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_decoder_flush(RtxDecoder* decoder);

/* Returns a slot to the decode pool after NVENC has consumed the frame. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_release_frame(RtxDecoder* decoder, uint64_t slot_index);

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_get_stats(RtxDecoder* decoder, RtxHwStats* stats);
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_decoder_last_error(RtxDecoder* decoder);

/* Verification helper: copies one decoded slot to host memory after the frame
 * has been received.  The container transcode loop never calls this; it exists
 * for the raw P010 artefact path and is accounted as device-to-host pixel
 * traffic in RtxHwStats.  destination_pitch is in bytes. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_download(RtxDecoder* decoder, const RtxFrameDesc* frame, uint8_t* destination,
                          size_t destination_pitch);

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_create(const RtxEncoderConfig* config, RtxEncoder** encoder);
RTX_SHIM_API void RTX_SHIM_CALL rtx_shim_encoder_destroy(RtxEncoder* encoder);

/* Encodes one device-resident frame.  The returned bitstream pointer remains
 * valid until the next encode/flush/destroy call. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_encode(RtxEncoder* encoder, const RtxFrameDesc* frame,
                        RtxPacketDesc* packet);

/* Verification helper: uploads a host NV12/P010 frame into the encoder's
 * staging surface and encodes it.  Counted as host pixel traffic; the container
 * path uses rtx_shim_encoder_encode() with device-resident slots instead. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_encode_host(RtxEncoder* encoder, const uint8_t* data, uint32_t pitch,
                             int32_t format, int64_t pts, RtxPacketDesc* packet);
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_flush(RtxEncoder* encoder, RtxPacketDesc* packet);

/* VPS/SPS/PPS (Annex-B) captured from NVENC after initialisation. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_get_sequence_header(RtxEncoder* encoder, uint8_t* buffer,
                                     size_t capacity, size_t* size);
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_get_stats(RtxEncoder* encoder, RtxHwStats* stats);
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_encoder_last_error(RtxEncoder* encoder);

/* Utility used by the CLI and by the import-table audit. */
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_result_name(RtxResult result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RTX_SHIM_ABI_H */
