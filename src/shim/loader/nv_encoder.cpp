/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * NVENC hardware encoder.  The NvEncodeAPI entry points are resolved at
 * runtime from nvEncodeAPI64.dll; the session is bound to the CUDA primary
 * context and every input frame is registered as a CUdeviceptr.  Pixel data is
 * never copied to host memory on the container transcode path: only the
 * compressed bitstream crosses the bus.  The explicit *_host helper exists for
 * the raw-file verification artefact and is accounted for separately.
 */

#include "rtx_shim_abi.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "dyn_library.h"
#include "nv_cuda_context.h"
#include "nv_func_tables.h"
#include "nv_trace.h"

#include "ffnvcodec/dynlink_cuda.h"
#include "ffnvcodec/nvEncodeAPI.h"

namespace {

using rtx_shim::DynLibrary;
using rtx_shim::cuda::Context;
using rtx_shim::cuda::DriverTable;
using rtx_shim::cuda::mapResult;

constexpr uint32_t kDefaultGop = 60;

#if defined(_WIN32)
constexpr DWORD kEncodeCompletionTimeoutMs = 15000;
#endif

const GUID& presetGuid(int preset) {
    switch (std::clamp(preset, 1, 7)) {
        case 1:
            return NV_ENC_PRESET_P1_GUID;
        case 2:
            return NV_ENC_PRESET_P2_GUID;
        case 3:
            return NV_ENC_PRESET_P3_GUID;
        case 4:
            return NV_ENC_PRESET_P4_GUID;
        case 5:
            return NV_ENC_PRESET_P5_GUID;
        case 6:
            return NV_ENC_PRESET_P6_GUID;
        default:
            return NV_ENC_PRESET_P7_GUID;
    }
}

NV_ENC_TUNING_INFO tuningForPreset(int preset) {
    // P1..P4 map to the latency-biased tuning, P5+ to quality.  A middle
    // preset must never yield NV_ENC_TUNING_INFO_UNDEFINED: the
    // GetEncodePresetConfigEx/InitializeEncoderEx path rejects it, which made
    // presets 3 and 4 fail session creation outright on driver 617.14.
    if (preset >= 5) {
        return NV_ENC_TUNING_INFO_HIGH_QUALITY;
    }
    return NV_ENC_TUNING_INFO_LOW_LATENCY;
}

RtxResult mapNvencStatus(NVENCSTATUS status) {
    switch (status) {
        case NV_ENC_SUCCESS:
            return RTX_OK;
        case NV_ENC_ERR_UNSUPPORTED_DEVICE:
        case NV_ENC_ERR_INVALID_ENCODERDEVICE:
        case NV_ENC_ERR_INVALID_DEVICE:
        case NV_ENC_ERR_NO_ENCODE_DEVICE:
            return RTX_ERR_ARCH_UNSUPPORTED;
        case NV_ENC_ERR_OUT_OF_MEMORY:
            return RTX_ERR_OUT_OF_MEMORY;
        case NV_ENC_ERR_INVALID_VERSION:
            return RTX_ERR_DRIVER_TOO_OLD;
        case NV_ENC_ERR_INVALID_PTR:
        case NV_ENC_ERR_INVALID_PARAM:
        case NV_ENC_ERR_INVALID_CALL:
        case NV_ENC_ERR_NOT_ENOUGH_BUFFER:
        case NV_ENC_ERR_UNSUPPORTED_PARAM:
            return RTX_ERR_INVALID_ARGUMENT;
        case NV_ENC_ERR_DEVICE_NOT_EXIST:
            return RTX_ERR_DEVICE_LOST;
        case NV_ENC_ERR_UNIMPLEMENTED:
            return RTX_ERR_UNSUPPORTED;
        default:
            return RTX_ERR_INTERNAL;
    }
}

NV_ENC_BUFFER_FORMAT bufferFormatFor(int32_t format) {
    return format == RTX_PIXEL_P010 ? NV_ENC_BUFFER_FORMAT_YUV420_10BIT
                                    : NV_ENC_BUFFER_FORMAT_NV12;
}

int bytesPerSample(int32_t format) { return format == RTX_PIXEL_P010 ? 2 : 1; }

}  // namespace

struct RtxEncoder {
    RtxEncoderConfig config{};
    Context cuda;
    DynLibrary library;
    NV_ENCODE_API_FUNCTION_LIST api{};
    void* session = nullptr;
    NV_ENC_OUTPUT_PTR bitstream = nullptr;
    std::vector<uint8_t> encoded;
    std::vector<uint8_t> sequence_header;
    std::unordered_map<uint64_t, NV_ENC_REGISTERED_PTR> registered;
    RtxHwStats stats{};
    std::string last_error;

    // Host staging surface used only by rtx_shim_encoder_encode_host().
    CUdeviceptr staging = 0;
    size_t staging_pitch = 0;
    size_t staging_bytes = 0;
    int staging_width = 0;
    int staging_height = 0;
    int32_t staging_format = RTX_PIXEL_UNKNOWN;

    // HDR metadata storage.  NVENC keeps the pointers until the picture is
    // submitted, so they must outlive the nvEncEncodePicture call.
    MASTERING_DISPLAY_INFO mastering{};
    CONTENT_LIGHT_LEVEL cll{};

    uint32_t frame_index = 0;
    bool eos_submitted = false;
    bool flushed = false;
    bool failed = false;

#if defined(_WIN32)
    // One auto-reset event per output bitstream buffer.  The session currently
    // owns a single buffer, so one event covers it.
    HANDLE completion_event = nullptr;
    NV_ENC_EVENT_PARAMS async_event{};
    bool async_registered = false;
#endif

    bool asyncEncode() const noexcept {
#if defined(_WIN32)
        return async_registered;
#else
        return false;
#endif
    }

    bool asyncRequested() const noexcept {
#if defined(_WIN32)
        return completion_event != nullptr;
#else
        return false;
#endif
    }

    bool waitForCompletion() {
#if defined(_WIN32)
        if (!async_registered) {
            return true;
        }
        const DWORD waited = WaitForSingleObject(completion_event, kEncodeCompletionTimeoutMs);
        if (waited != WAIT_OBJECT_0) {
            failed = true;
            last_error = "NVENC asynchronous completion event timed out";
            return false;
        }
#endif
        return true;
    }

    std::string nvError(const char* operation, NVENCSTATUS status) const {
        std::string message = operation;
        message += " failed (NVENC status ";
        message += std::to_string(static_cast<int>(status));
        message += ')';
        if (api.nvEncGetLastErrorString != nullptr && session != nullptr) {
            const char* detail = api.nvEncGetLastErrorString(session);
            if (detail != nullptr && detail[0] != '\0') {
                message += ": ";
                message += detail;
            }
        }
        return message;
    }

    RtxResult fail(RtxResult result, const std::string& message) {
        last_error = message;
        failed = true;
        return result;
    }

    bool buildConfig(NV_ENC_CONFIG* out) {
        const bool ten_bit = config.input_format == RTX_PIXEL_P010;
        const bool hevc = config.codec == 0;
        const bool h264 = config.codec == 2;
        if (!hevc && !h264) {
            last_error = "stage-1 NVENC supports HEVC and H.264 sessions";
            return false;
        }

        const GUID& codec = hevc ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
        const NV_ENC_TUNING_INFO tuning = tuningForPreset(config.preset);

        NV_ENC_PRESET_CONFIG preset_config{};
        preset_config.version = NV_ENC_PRESET_CONFIG_VER;
        preset_config.presetCfg.version = NV_ENC_CONFIG_VER;
        NVENCSTATUS status = NV_ENC_ERR_UNIMPLEMENTED;
        if (api.nvEncGetEncodePresetConfigEx != nullptr &&
            tuning != NV_ENC_TUNING_INFO_UNDEFINED) {
            status = api.nvEncGetEncodePresetConfigEx(session, codec, presetGuid(config.preset),
                                                      tuning, &preset_config);
        }
        if (status != NV_ENC_SUCCESS && api.nvEncGetEncodePresetConfig != nullptr) {
            status = api.nvEncGetEncodePresetConfig(session, codec, presetGuid(config.preset),
                                                    &preset_config);
        }
        if (status != NV_ENC_SUCCESS) {
            last_error = nvError("NvEncGetEncodePresetConfig", status);
            return false;
        }
        *out = preset_config.presetCfg;
        out->version = NV_ENC_CONFIG_VER;
        out->profileGUID = hevc ? (ten_bit ? NV_ENC_HEVC_PROFILE_MAIN10_GUID
                                          : NV_ENC_HEVC_PROFILE_MAIN_GUID)
                                : NV_ENC_H264_PROFILE_HIGH_GUID;
        out->gopLength = config.gop_length > 0 ? static_cast<uint32_t>(config.gop_length)
                                               : kDefaultGop;
        out->frameIntervalP = 1;  // IPP: one compressed picture per input frame.
        out->frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
        out->mvPrecision = NV_ENC_MV_PRECISION_DEFAULT;

        const uint32_t fps_num = static_cast<uint32_t>(std::max(1, config.fps_num));
        const uint32_t fps_den = static_cast<uint32_t>(std::max(1, config.fps_den));
        uint32_t bitrate = config.bitrate_kbps > 0
                               ? static_cast<uint32_t>(config.bitrate_kbps) * 1000u
                               : static_cast<uint32_t>(
                                     std::max<double>(2000000.0,
                                                      static_cast<double>(config.width) *
                                                          config.height * fps_num / fps_den *
                                                          0.09));
        if (bitrate < 500000u) {
            bitrate = 500000u;
        }

        NV_ENC_RC_PARAMS& rc = out->rcParams;
        switch (config.rate_control) {
            case 1:
                rc.rateControlMode = NV_ENC_PARAMS_RC_VBR;
                break;
            case 2:
                rc.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
                break;
            default:
                rc.rateControlMode = NV_ENC_PARAMS_RC_CBR;
                break;
        }
        rc.averageBitRate = bitrate;
        rc.maxBitRate = config.rate_control == 1 ? static_cast<uint32_t>(bitrate * 1.5) : bitrate;
        rc.vbvBufferSize = bitrate * 2u;
        if (config.rate_control == 2) {
            const int32_t qp = std::clamp(config.qp > 0 ? config.qp : 24, 0, 51);
            rc.constQP.qpIntra = static_cast<uint32_t>(qp);
            rc.constQP.qpInterP = static_cast<uint32_t>(qp);
            rc.constQP.qpInterB = static_cast<uint32_t>(qp);
        }
        rc.enableMinQP = 0;
        rc.enableMaxQP = 0;
        rc.enableInitialRCQP = 0;
        rc.enableAQ = 0;
        rc.enableTemporalAQ = 0;
        rc.enableLookahead = 0;
        rc.zeroReorderDelay = 1;

        const bool hdr = config.hdr.enabled != 0;
        // The shim carries H.265 colour code points.  Use them for both HDR
        // and SDR sessions so the VUI matches the container flags copied from
        // the source instead of always declaring BT.709 for SDR.
        const auto colourValue = [](int32_t configured, uint32_t fallback) {
            return configured > 0 ? static_cast<uint32_t>(configured) : fallback;
        };
        const uint32_t primaries =
            colourValue(config.hdr.primaries, hdr ? NV_ENC_VUI_COLOR_PRIMARIES_BT2020
                                                  : NV_ENC_VUI_COLOR_PRIMARIES_BT709);
        const uint32_t transfer = colourValue(
            config.hdr.transfer, hdr ? NV_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE2084
                                     : NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709);
        const uint32_t matrix =
            colourValue(config.hdr.matrix, hdr ? NV_ENC_VUI_MATRIX_COEFFS_BT2020_NCL
                                               : NV_ENC_VUI_MATRIX_COEFFS_BT709);
        const uint32_t full_range = config.hdr.range == 1 ? 1u : 0u;

        if (hevc) {
            NV_ENC_CONFIG_HEVC& hevc_config = out->encodeCodecConfig.hevcConfig;
            hevc_config.chromaFormatIDC = 1;
            hevc_config.level = NV_ENC_LEVEL_AUTOSELECT;
            hevc_config.tier = config.tier == 1 ? NV_ENC_TIER_HEVC_HIGH : NV_ENC_TIER_HEVC_MAIN;
            hevc_config.idrPeriod = out->gopLength;
            hevc_config.repeatSPSPPS = 1;
            hevc_config.outputAUD = 1;
            hevc_config.maxNumRefFramesInDPB = 4;
            hevc_config.inputBitDepth = ten_bit ? NV_ENC_BIT_DEPTH_10 : NV_ENC_BIT_DEPTH_8;
            hevc_config.outputBitDepth = hevc_config.inputBitDepth;
            hevc_config.outputMasteringDisplay = hdr ? 1u : 0u;
            hevc_config.outputMaxCll = hdr ? 1u : 0u;
            NV_ENC_CONFIG_HEVC_VUI_PARAMETERS& vui = hevc_config.hevcVUIParameters;
            vui.videoSignalTypePresentFlag = 1;
            vui.videoFullRangeFlag = full_range;
            vui.colourDescriptionPresentFlag = 1;
            vui.colourPrimaries = static_cast<NV_ENC_VUI_COLOR_PRIMARIES>(primaries);
            vui.transferCharacteristics =
                static_cast<NV_ENC_VUI_TRANSFER_CHARACTERISTIC>(transfer);
            vui.colourMatrix = static_cast<NV_ENC_VUI_MATRIX_COEFFS>(matrix);
            if (hdr) {
                // UHD HDR10 mandates top-left chroma siting (AVCHROMA_LOC_TOPLEFT
                // == 2).  Leaving the MPEG-2 centre default makes players
                // interpolate chroma half a sample off, which shows up as red
                // and green edge bleeding on 4K masters.
                vui.chromaSampleLocationFlag = 1;
                vui.chromaSampleLocationTop = 2;
                vui.chromaSampleLocationBot = 2;
            }
        } else {
            NV_ENC_CONFIG_H264& h264_config = out->encodeCodecConfig.h264Config;
            h264_config.chromaFormatIDC = 1;
            h264_config.level = NV_ENC_LEVEL_AUTOSELECT;
            h264_config.idrPeriod = out->gopLength;
            h264_config.repeatSPSPPS = 1;
            h264_config.outputAUD = 1;
            h264_config.maxNumRefFrames = 4;
            h264_config.inputBitDepth = NV_ENC_BIT_DEPTH_8;
            h264_config.outputBitDepth = NV_ENC_BIT_DEPTH_8;
            NV_ENC_CONFIG_H264_VUI_PARAMETERS& vui = h264_config.h264VUIParameters;
            vui.videoSignalTypePresentFlag = 1;
            vui.videoFullRangeFlag = full_range;
            vui.colourDescriptionPresentFlag = 1;
            vui.colourPrimaries = static_cast<NV_ENC_VUI_COLOR_PRIMARIES>(primaries);
            vui.transferCharacteristics =
                static_cast<NV_ENC_VUI_TRANSFER_CHARACTERISTIC>(transfer);
            vui.colourMatrix = static_cast<NV_ENC_VUI_MATRIX_COEFFS>(matrix);
        }

        if (hdr) {
            mastering.g.x = config.hdr.green_x;
            mastering.g.y = config.hdr.green_y;
            mastering.b.x = config.hdr.blue_x;
            mastering.b.y = config.hdr.blue_y;
            mastering.r.x = config.hdr.red_x;
            mastering.r.y = config.hdr.red_y;
            mastering.whitePoint.x = config.hdr.white_x;
            mastering.whitePoint.y = config.hdr.white_y;
            mastering.maxLuma = config.hdr.max_luminance != 0 ? config.hdr.max_luminance : 10000000u;
            mastering.minLuma = config.hdr.min_luminance;
            cll.maxContentLightLevel = config.hdr.max_cll;
            cll.maxPicAverageLightLevel = config.hdr.max_fall;
        }
        return true;
    }

    bool fetchSequenceHeader() {
        if (api.nvEncGetSequenceParams == nullptr || session == nullptr) {
            return false;
        }
        uint8_t buffer[NV_MAX_SEQ_HDR_LEN * 4] = {};
        uint32_t size = 0;
        NV_ENC_SEQUENCE_PARAM_PAYLOAD payload{};
        payload.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
        payload.inBufferSize = static_cast<uint32_t>(sizeof(buffer));
        payload.spsppsBuffer = buffer;
        payload.outSPSPPSPayloadSize = &size;
        const NVENCSTATUS status = api.nvEncGetSequenceParams(session, &payload);
        if (status != NV_ENC_SUCCESS || size == 0 || size > sizeof(buffer)) {
            return false;
        }
        sequence_header.assign(buffer, buffer + size);
        return true;
    }

    NV_ENC_REGISTERED_PTR registerInput(uint64_t pointer, const RtxFrameDesc& frame) {
        const auto found = registered.find(pointer);
        if (found != registered.end()) {
            return found->second;
        }
        NV_ENC_REGISTER_RESOURCE resource{};
        resource.version = NV_ENC_REGISTER_RESOURCE_VER;
        resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
        resource.width = static_cast<uint32_t>(frame.width);
        resource.height = static_cast<uint32_t>(frame.height);
        resource.pitch = frame.pitches[0];
        resource.resourceToRegister = reinterpret_cast<void*>(static_cast<uintptr_t>(pointer));
        resource.bufferFormat = bufferFormatFor(frame.format);
        resource.bufferUsage = NV_ENC_INPUT_IMAGE;
        const NVENCSTATUS status = api.nvEncRegisterResource(session, &resource);
        if (status != NV_ENC_SUCCESS) {
            last_error = nvError("NvEncRegisterResource", status);
            return nullptr;
        }
        registered.emplace(pointer, resource.registeredResource);
        return resource.registeredResource;
    }

    RtxResult encodeDeviceFrame(const RtxFrameDesc& frame, RtxPacketDesc* out) {
        if (frame.width <= 0 || frame.height <= 0 || frame.planes[0] == 0 ||
            frame.pitches[0] == 0) {
            return RTX_ERR_INVALID_ARGUMENT;
        }
        if (frame.format != RTX_PIXEL_NV12 && frame.format != RTX_PIXEL_P010) {
            return RTX_ERR_UNSUPPORTED;
        }
        const int32_t expected = config.input_format == RTX_PIXEL_P010 ? RTX_PIXEL_P010
                                                                       : RTX_PIXEL_NV12;
        if (frame.format != expected) {
            return fail(RTX_ERR_UNSUPPORTED,
                        "frame pixel format does not match the NVENC session bit depth");
        }
        if (eos_submitted) {
            return RTX_ERR_IO;
        }

        NV_ENC_REGISTERED_PTR resource = registerInput(frame.planes[0], frame);
        if (resource == nullptr) {
            return fail(RTX_ERR_INTERNAL, last_error);
        }

        NV_ENC_MAP_INPUT_RESOURCE map{};
        map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map.registeredResource = resource;
        NVENCSTATUS status = api.nvEncMapInputResource(session, &map);
        if (status != NV_ENC_SUCCESS) {
            return fail(RTX_ERR_INTERNAL, nvError("NvEncMapInputResource", status));
        }

        NV_ENC_PIC_PARAMS picture{};
        picture.version = NV_ENC_PIC_PARAMS_VER;
        picture.inputWidth = static_cast<uint32_t>(frame.width);
        picture.inputHeight = static_cast<uint32_t>(frame.height);
        picture.inputPitch = frame.pitches[0];
        picture.inputBuffer = map.mappedResource;
        picture.outputBitstream = bitstream;
        picture.bufferFmt = map.mappedBufferFmt;
        picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        picture.inputTimeStamp = static_cast<uint64_t>(frame.pts);
        picture.frameIdx = frame_index++;
#if defined(_WIN32)
        if (async_registered) {
            picture.completionEvent = completion_event;
        }
#endif
        if (config.codec == 0 && config.hdr.enabled != 0) {
            picture.codecPicParams.hevcPicParams.pMasteringDisplay = &mastering;
            picture.codecPicParams.hevcPicParams.pMaxCll = &cll;
        }

        status = api.nvEncEncodePicture(session, &picture);
        RTX_TRACE("encoder: nvEncEncodePicture -> %d (pts %lld)", static_cast<int>(status),
                  static_cast<long long>(frame.pts));
        if (status != NV_ENC_SUCCESS) {
            api.nvEncUnmapInputResource(session, map.mappedResource);
            return fail(mapNvencStatus(status), nvError("NvEncEncodePicture", status));
        }
        // The mapped input must stay alive until the hardware has consumed it,
        // so the completion event is the unmap barrier in asynchronous mode.
        if (!waitForCompletion()) {
            api.nvEncUnmapInputResource(session, map.mappedResource);
            return fail(RTX_ERR_INTERNAL, last_error);
        }
        api.nvEncUnmapInputResource(session, map.mappedResource);

        NV_ENC_LOCK_BITSTREAM lock{};
        lock.version = NV_ENC_LOCK_BITSTREAM_VER;
        lock.outputBitstream = bitstream;
        status = api.nvEncLockBitstream(session, &lock);
        if (status != NV_ENC_SUCCESS) {
            return fail(RTX_ERR_INTERNAL, nvError("NvEncLockBitstream", status));
        }
        if (lock.bitstreamSizeInBytes > 0) {
            const uint8_t* bytes = static_cast<const uint8_t*>(lock.bitstreamBufferPtr);
            encoded.assign(bytes, bytes + lock.bitstreamSizeInBytes);
        } else {
            encoded.clear();
        }
        const size_t size = encoded.size();
        const bool keyframe = lock.pictureType == NV_ENC_PIC_TYPE_IDR ||
                              lock.pictureType == NV_ENC_PIC_TYPE_I;
        status = api.nvEncUnlockBitstream(session, bitstream);
        if (status != NV_ENC_SUCCESS) {
            return fail(RTX_ERR_INTERNAL, nvError("NvEncUnlockBitstream", status));
        }
        if (size == 0) {
            return RTX_ERR_NOT_FOUND;
        }
        if (sequence_header.empty()) {
            (void)fetchSequenceHeader();
        }

        out->data = encoded.data();
        out->size = size;
        out->pts = frame.pts;
        out->dts = frame.pts;
        out->duration = 0;
        out->keyframe = keyframe ? 1 : 0;
        ++stats.frames_in;
        ++stats.frames_out;
        stats.bitstream_bytes += size;
        return RTX_OK;
    }

    bool ensureStaging(int width, int height, int32_t format) {
        if (staging != 0 && staging_width == width && staging_height == height &&
            staging_format == format) {
            return true;
        }
        if (staging != 0) {
            cuda.driver().cuMemFree(staging);
            staging = 0;
        }
        const size_t bytes = static_cast<size_t>(width) * bytesPerSample(format);
        auto result = cuda.driver().cuMemAllocPitch(
            &staging, &staging_pitch, bytes, static_cast<size_t>(height) * 3 / 2, 16);
        if (result != CUDA_SUCCESS) {
            last_error = "cuMemAllocPitch failed for the NVENC host staging surface";
            return false;
        }
        staging_bytes = staging_pitch * static_cast<size_t>(height) * 3 / 2;
        staging_width = width;
        staging_height = height;
        staging_format = format;
        return true;
    }

    void destroyRegistered() {
        if (session != nullptr && api.nvEncUnregisterResource != nullptr) {
            for (const auto& entry : registered) {
                api.nvEncUnregisterResource(session, entry.second);
            }
        }
        registered.clear();
    }
};

extern "C" {

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_create(const RtxEncoderConfig* config, RtxEncoder** encoder) {
    if (encoder == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *encoder = nullptr;
    if (config == nullptr || config->width <= 0 || config->height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (config->codec != 0 && config->codec != 2) {
        return RTX_ERR_UNSUPPORTED;
    }
    if (config->input_format != RTX_PIXEL_NV12 && config->input_format != RTX_PIXEL_P010) {
        return RTX_ERR_UNSUPPORTED;
    }
    auto* self = new (std::nothrow) RtxEncoder();
    if (self == nullptr) {
        return RTX_ERR_OUT_OF_MEMORY;
    }
    self->config = *config;
    if (self->config.fps_num <= 0 || self->config.fps_den <= 0) {
        self->config.fps_num = 30;
        self->config.fps_den = 1;
    }

    std::string error;
    if (!self->cuda.open(&error)) {
        self->last_error = error;
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    RTX_TRACE("encoder: CUDA context ready (%s)", self->cuda.deviceName().c_str());
    if (!rtx_shim::openNvidiaLibrary(self->library, "nvEncodeAPI64.dll",
                                     "libnvidia-encode.so.1", true, &error)) {
        self->last_error = error;
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    const auto create_instance = reinterpret_cast<NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*)>(
        self->library.symbol("NvEncodeAPICreateInstance"));
    if (create_instance == nullptr) {
        self->last_error = "nvEncodeAPI is missing NvEncodeAPICreateInstance";
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    self->api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    RTX_TRACE("encoder: NvEncodeAPICreateInstance");
    NVENCSTATUS status = create_instance(&self->api);
    if (status != NV_ENC_SUCCESS) {
        self->last_error = self->nvError("NvEncodeAPICreateInstance", status);
        delete self;
        return RTX_ERR_DRIVER_TOO_OLD;
    }
    if (self->api.nvEncOpenEncodeSessionEx == nullptr ||
        self->api.nvEncInitializeEncoder == nullptr ||
        self->api.nvEncCreateBitstreamBuffer == nullptr ||
        self->api.nvEncEncodePicture == nullptr || self->api.nvEncLockBitstream == nullptr ||
        self->api.nvEncUnlockBitstream == nullptr ||
        self->api.nvEncMapInputResource == nullptr ||
        self->api.nvEncUnmapInputResource == nullptr ||
        self->api.nvEncRegisterResource == nullptr ||
        self->api.nvEncUnregisterResource == nullptr ||
        self->api.nvEncDestroyBitstreamBuffer == nullptr ||
        self->api.nvEncDestroyEncoder == nullptr) {
        self->last_error = "nvEncodeAPI function table is incomplete";
        delete self;
        return RTX_ERR_DRIVER_TOO_OLD;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session{};
    session.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    session.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    session.device = self->cuda.handle();
    session.apiVersion = NVENCAPI_VERSION;
    RTX_TRACE("encoder: nvEncOpenEncodeSessionEx codec=%d %dx%d %s", config->codec,
              config->width, config->height,
              config->input_format == RTX_PIXEL_P010 ? "P010" : "NV12");
    status = self->api.nvEncOpenEncodeSessionEx(&session, &self->session);
    if (status != NV_ENC_SUCCESS) {
        self->last_error = self->nvError("NvEncOpenEncodeSessionEx", status);
        RTX_TRACE("encoder: nvEncOpenEncodeSessionEx -> %d: %s", static_cast<int>(status),
                  self->last_error.c_str());
        const RtxResult mapped = mapNvencStatus(status);
        delete self;
        return mapped == RTX_OK ? RTX_ERR_INTERNAL : mapped;
    }
    RTX_TRACE("encoder: nvEncOpenEncodeSessionEx -> ok");

#if defined(_WIN32)
    // WDDM asynchronous mode: completion is delivered through a kernel event
    // instead of an internal driver wait, so nvEncEncodePicture returns as soon
    // as the work is queued.  The event is created before initialisation (the
    // flag is part of NV_ENC_INITIALIZE_PARAMS) and registered afterwards,
    // because NvEncRegisterAsyncEvent requires an initialised encoder.
    if (self->api.nvEncRegisterAsyncEvent == nullptr ||
        self->api.nvEncUnregisterAsyncEvent == nullptr) {
        self->last_error = "nvEncodeAPI does not expose the asynchronous event API";
        (void)self->api.nvEncDestroyEncoder(self->session);
        self->session = nullptr;
        delete self;
        return RTX_ERR_DRIVER_TOO_OLD;
    }
    self->completion_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (self->completion_event == nullptr) {
        self->last_error = "CreateEventW failed for the NVENC completion event";
        (void)self->api.nvEncDestroyEncoder(self->session);
        self->session = nullptr;
        delete self;
        return RTX_ERR_INTERNAL;
    }
    self->async_event.version = NV_ENC_EVENT_PARAMS_VER;
    self->async_event.completionEvent = self->completion_event;
#endif

    NV_ENC_CONFIG encode_config{};
    encode_config.version = NV_ENC_CONFIG_VER;
    RTX_TRACE("encoder: building the NV_ENC_CONFIG");
    if (!self->buildConfig(&encode_config)) {
        delete self;
        return RTX_ERR_UNSUPPORTED;
    }
    RTX_TRACE("encoder: NV_ENC_CONFIG ready (gop=%u, rc=%d)", encode_config.gopLength,
              static_cast<int>(encode_config.rcParams.rateControlMode));
    NV_ENC_INITIALIZE_PARAMS initialize{};
    initialize.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initialize.encodeGUID = config->codec == 0 ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
    initialize.presetGUID = presetGuid(config->preset);
    initialize.encodeWidth = static_cast<uint32_t>(config->width);
    initialize.encodeHeight = static_cast<uint32_t>(config->height);
    initialize.darWidth = static_cast<uint32_t>(config->width);
    initialize.darHeight = static_cast<uint32_t>(config->height);
    initialize.frameRateNum = static_cast<uint32_t>(self->config.fps_num);
    initialize.frameRateDen = static_cast<uint32_t>(self->config.fps_den);
    initialize.enableEncodeAsync = self->asyncRequested() ? 1 : 0;
    initialize.enablePTD = 1;
    initialize.encodeConfig = &encode_config;
    initialize.maxEncodeWidth = initialize.encodeWidth;
    initialize.maxEncodeHeight = initialize.encodeHeight;
    initialize.tuningInfo = tuningForPreset(config->preset);
    initialize.bufferFormat = bufferFormatFor(config->input_format);

    status = self->api.nvEncInitializeEncoder(self->session, &initialize);
    RTX_TRACE("encoder: nvEncInitializeEncoder -> %d", static_cast<int>(status));
    if (status != NV_ENC_SUCCESS) {
        self->last_error = self->nvError("NvEncInitializeEncoder", status);
        const RtxResult mapped = mapNvencStatus(status);
        delete self;
        return mapped == RTX_OK ? RTX_ERR_UNSUPPORTED : mapped;
    }

    NV_ENC_CREATE_BITSTREAM_BUFFER buffer{};
    buffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    status = self->api.nvEncCreateBitstreamBuffer(self->session, &buffer);
    if (status != NV_ENC_SUCCESS) {
        self->last_error = self->nvError("NvEncCreateBitstreamBuffer", status);
        delete self;
        return mapNvencStatus(status);
    }
    self->bitstream = buffer.bitstreamBuffer;

#if defined(_WIN32)
    if (self->asyncRequested()) {
        status = self->api.nvEncRegisterAsyncEvent(self->session, &self->async_event);
        if (status != NV_ENC_SUCCESS) {
            self->last_error = self->nvError("NvEncRegisterAsyncEvent", status);
            RTX_TRACE("encoder: nvEncRegisterAsyncEvent -> %d: %s", static_cast<int>(status),
                      self->last_error.c_str());
            (void)self->api.nvEncDestroyBitstreamBuffer(self->session, self->bitstream);
            self->bitstream = nullptr;
            CloseHandle(self->completion_event);
            self->completion_event = nullptr;
            (void)self->api.nvEncDestroyEncoder(self->session);
            self->session = nullptr;
            delete self;
            return mapNvencStatus(status);
        }
        self->async_registered = true;
        RTX_TRACE("encoder: WDDM asynchronous completion event registered");
    }
#endif

    (void)self->fetchSequenceHeader();
    RTX_TRACE("encoder: session ready (sequence header %zu bytes)", self->sequence_header.size());
    *encoder = self;
    return RTX_OK;
}

RTX_SHIM_API void RTX_SHIM_CALL rtx_shim_encoder_destroy(RtxEncoder* encoder) {
    if (encoder == nullptr) {
        return;
    }
#if defined(_WIN32)
    if (encoder->session != nullptr && encoder->async_registered &&
        encoder->api.nvEncUnregisterAsyncEvent != nullptr) {
        (void)encoder->api.nvEncUnregisterAsyncEvent(encoder->session, &encoder->async_event);
        encoder->async_registered = false;
    }
    if (encoder->completion_event != nullptr) {
        CloseHandle(encoder->completion_event);
        encoder->completion_event = nullptr;
    }
#endif
    encoder->destroyRegistered();
    if (encoder->bitstream != nullptr && encoder->api.nvEncDestroyBitstreamBuffer != nullptr) {
        encoder->api.nvEncDestroyBitstreamBuffer(encoder->session, encoder->bitstream);
        encoder->bitstream = nullptr;
    }
    if (encoder->session != nullptr && encoder->api.nvEncDestroyEncoder != nullptr) {
        encoder->api.nvEncDestroyEncoder(encoder->session);
        encoder->session = nullptr;
    }
    if (encoder->staging != 0) {
        encoder->cuda.driver().cuMemFree(encoder->staging);
        encoder->staging = 0;
    }
    delete encoder;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_encode(RtxEncoder* encoder, const RtxFrameDesc* frame, RtxPacketDesc* packet) {
    if (encoder == nullptr || frame == nullptr || packet == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (encoder->failed) {
        return RTX_ERR_INTERNAL;
    }
    return encoder->encodeDeviceFrame(*frame, packet);
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_encode_host(RtxEncoder* encoder, const uint8_t* data, uint32_t pitch,
                             int32_t format, int64_t pts, RtxPacketDesc* packet) {
    if (encoder == nullptr || data == nullptr || pitch == 0 || packet == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (encoder->failed) {
        return RTX_ERR_INTERNAL;
    }
    if (format != encoder->config.input_format) {
        return encoder->fail(RTX_ERR_UNSUPPORTED,
                             "host frame format does not match the NVENC session");
    }
    if (!encoder->ensureStaging(encoder->config.width, encoder->config.height, format)) {
        return encoder->fail(RTX_ERR_OUT_OF_MEMORY, encoder->last_error);
    }

    const size_t sample_bytes = static_cast<size_t>(bytesPerSample(format));
    const size_t row_bytes = static_cast<size_t>(encoder->config.width) * sample_bytes;
    DriverTable& driver = encoder->cuda.driver();
    rtx_shim::cuda::CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_HOST;
    copy.srcHost = data;
    copy.srcPitch = pitch;
    copy.dstMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_DEVICE;
    copy.dstDevice = encoder->staging;
    copy.dstPitch = encoder->staging_pitch;
    copy.WidthInBytes = row_bytes;
    copy.Height = static_cast<size_t>(encoder->config.height);
    auto result = driver.cuMemcpy2DAsync(&copy, nullptr);
    if (result == CUDA_SUCCESS) {
        rtx_shim::cuda::CUDA_MEMCPY2D chroma = copy;
        chroma.srcHost = data + static_cast<size_t>(pitch) * encoder->config.height;
        chroma.dstDevice = encoder->staging + static_cast<CUdeviceptr>(encoder->staging_pitch) *
                                                  static_cast<CUdeviceptr>(encoder->config.height);
        chroma.Height = static_cast<size_t>(encoder->config.height) / 2;
        result = driver.cuMemcpy2DAsync(&chroma, nullptr);
    }
    if (result != CUDA_SUCCESS) {
        return encoder->fail(RTX_ERR_INTERNAL, "host-to-device pixel upload failed");
    }
    encoder->stats.host_to_device_pixel_bytes += row_bytes * encoder->config.height * 3 / 2;
    ++encoder->stats.host_pixel_copies;

    RtxFrameDesc device{};
    device.planes[0] = encoder->staging;
    device.planes[1] = encoder->staging + static_cast<CUdeviceptr>(encoder->staging_pitch) *
                                            static_cast<CUdeviceptr>(encoder->config.height);
    device.pitches[0] = static_cast<uint32_t>(encoder->staging_pitch);
    device.pitches[1] = device.pitches[0];
    device.width = encoder->config.width;
    device.height = encoder->config.height;
    device.format = format;
    device.bit_depth = format == RTX_PIXEL_P010 ? 10 : 8;
    device.pts = pts;
    device.slot_index = 0;
    return encoder->encodeDeviceFrame(device, packet);
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_flush(RtxEncoder* encoder, RtxPacketDesc* packet) {
    if (encoder == nullptr || packet == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (encoder->flushed) {
        return RTX_ERR_NOT_FOUND;
    }
    // Every picture is awaited through its completion event, and the session
    // uses frameIntervalP = 1 with zeroReorderDelay, so no delayed picture can
    // be pending at EOS.  Locking the bitstream buffer again would block
    // forever because the encoder produces no further output picture.  EOS
    // only lets the encoder release its references; flush is a no-op that
    // reports "nothing more to read".
    if (!encoder->eos_submitted) {
        NV_ENC_PIC_PARAMS picture{};
        picture.version = NV_ENC_PIC_PARAMS_VER;
        picture.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
        picture.outputBitstream = encoder->bitstream;
#if defined(_WIN32)
        // The asynchronous API rejects any picture without a completion event,
        // including the EOS marker ("Completion event is NULL").
        if (encoder->async_registered) {
            picture.completionEvent = encoder->completion_event;
        }
#endif
        RTX_TRACE("encoder: nvEncEncodePicture(EOS) submitting");
        const NVENCSTATUS status = encoder->api.nvEncEncodePicture(encoder->session, &picture);
        RTX_TRACE("encoder: nvEncEncodePicture(EOS) -> %d", static_cast<int>(status));
        if (status != NV_ENC_SUCCESS) {
            return encoder->fail(RTX_ERR_INTERNAL,
                                 encoder->nvError("NvEncEncodePicture(EOS)", status));
        }
        if (!encoder->waitForCompletion()) {
            return encoder->fail(RTX_ERR_INTERNAL, encoder->last_error);
        }
        encoder->eos_submitted = true;
    }
    encoder->flushed = true;
    packet->data = nullptr;
    packet->size = 0;
    packet->pts = 0;
    packet->dts = 0;
    packet->duration = 0;
    packet->keyframe = 0;
    return RTX_ERR_NOT_FOUND;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_get_sequence_header(RtxEncoder* encoder, uint8_t* buffer, size_t capacity,
                                     size_t* size) {
    if (encoder == nullptr || size == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (encoder->sequence_header.empty()) {
        (void)encoder->fetchSequenceHeader();
    }
    if (encoder->sequence_header.empty()) {
        *size = 0;
        return RTX_ERR_NOT_FOUND;
    }
    *size = encoder->sequence_header.size();
    if (buffer == nullptr) {
        return RTX_OK;
    }
    if (capacity < encoder->sequence_header.size()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::memcpy(buffer, encoder->sequence_header.data(), encoder->sequence_header.size());
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_encoder_get_stats(RtxEncoder* encoder, RtxHwStats* stats) {
    if (encoder == nullptr || stats == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *stats = encoder->stats;
    return RTX_OK;
}

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_encoder_last_error(RtxEncoder* encoder) {
    if (encoder == nullptr) {
        return "invalid RtxEncoder handle";
    }
    return encoder->last_error.c_str();
}

}  // extern "C"
