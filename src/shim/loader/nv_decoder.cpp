/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * NVDEC (cuvid) hardware decoder.  Every entry point is resolved at runtime
 * from nvcuvid.dll.  Decoded surfaces are mapped, copied device-to-device into
 * a pre-allocated VRAM pool with cuMemcpy2DAsync, and unmapped once the export
 * event signals; no pixel data is transferred to host memory.  The parser
 * callback never blocks on the GPU.
 */

#include "rtx_shim_abi.h"

// Hardware NVDEC surface decoding and asynchronous cuMemcpy2DAsync into GPU VRAM pool.
// Bypasses host staging memory entirely, delivering zero-copy NV12/P010 surfaces to NGX and NVENC.
// Surface mappings must be strictly unmapped upon export event signaling to prevent VRAM leaks.
#include "dyn_library.h"
#include "nv_cuda_context.h"
#include "nv_func_tables.h"
#include "nv_trace.h"

#include "ffnvcodec/dynlink_cuda.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnested-anon-types"
#endif
#include "ffnvcodec/dynlink_nvcuvid.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <tuple>
#include <vector>

namespace {

using rtx_shim::DynLibrary;
using rtx_shim::cuda::Context;
using rtx_shim::cuda::DriverTable;
using rtx_shim::cuda::mapResult;

cudaVideoCodec mapCodec(int32_t codec) {
    switch (codec) {
        case RTX_CODEC_H264:
            return cudaVideoCodec_H264;
        case RTX_CODEC_HEVC:
            return cudaVideoCodec_HEVC;
        case RTX_CODEC_AV1:
            return cudaVideoCodec_AV1;
        default:
            return cudaVideoCodec_NumCodecs;
    }
}
}  // namespace

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: rtx_shim_abi.h 导出的不透明 NVDEC 硬解句柄，管理 CUvideoparser/CUvideodecoder 与导出槽位生命周期。
// Invariant: 仅通过 rtx_shim_decoder_create/destroy 管理；所有 CUDA/CUVID 句柄按逆序释放。
struct RtxDecoder {
    RtxDecoderConfig config{};
    Context cuda;
    DynLibrary cuvid;
    tcuvidCreateVideoParser* createParser = nullptr;
    tcuvidParseVideoData* parseVideoData = nullptr;
    tcuvidDestroyVideoParser* destroyParser = nullptr;
    tcuvidCreateDecoder* createDecoder = nullptr;
    tcuvidDestroyDecoder* destroyDecoder = nullptr;
    tcuvidDecodePicture* decodePicture = nullptr;
    tcuvidMapVideoFrame64* mapVideoFrame = nullptr;
    tcuvidUnmapVideoFrame64* unmapVideoFrame = nullptr;
    tcuvidGetDecoderCaps* getDecoderCaps = nullptr;

    bool resolveFunctions(const DynLibrary& library, std::string* error) {
        createParser = rtx_shim::dynSymbolAs<tcuvidCreateVideoParser*>(library, "cuvidCreateVideoParser");
        parseVideoData = rtx_shim::dynSymbolAs<tcuvidParseVideoData*>(library, "cuvidParseVideoData");
        destroyParser = rtx_shim::dynSymbolAs<tcuvidDestroyVideoParser*>(library, "cuvidDestroyVideoParser");
        createDecoder = rtx_shim::dynSymbolAs<tcuvidCreateDecoder*>(library, "cuvidCreateDecoder");
        destroyDecoder = rtx_shim::dynSymbolAs<tcuvidDestroyDecoder*>(library, "cuvidDestroyDecoder");
        decodePicture = rtx_shim::dynSymbolAs<tcuvidDecodePicture*>(library, "cuvidDecodePicture");
        mapVideoFrame = rtx_shim::dynSymbolAs<tcuvidMapVideoFrame64*>(library, "cuvidMapVideoFrame64");
        unmapVideoFrame = rtx_shim::dynSymbolAs<tcuvidUnmapVideoFrame64*>(library, "cuvidUnmapVideoFrame64");
        getDecoderCaps = rtx_shim::dynSymbolAs<tcuvidGetDecoderCaps*>(library, "cuvidGetDecoderCaps");
        if (!createParser || !parseVideoData || !destroyParser || !createDecoder ||
            !destroyDecoder || !decodePicture || !mapVideoFrame || !unmapVideoFrame) {
            if (error) *error = "nvcuvid is missing one or more cuvid entry points";
            return false;
        }
        return true;
    }
    CUvideoparser parser = nullptr;
    CUvideodecoder decoder = nullptr;
    CUstream stream = nullptr;
    std::vector<std::tuple<CUdeviceptr, size_t, size_t, bool, rtx_shim::cuda::CUevent, bool>> slots;
    std::deque<uint32_t> free_slots;
    std::deque<RtxFrameDesc> ready;
    RtxHwStats stats{};
    std::string last_error;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    unsigned int surface_count = 32;
    bool end_of_stream = false;
// The CUVID parser hands out CurrPicIdx values from its own surface space,
// so the decoder must be created with exactly the same count.  A smaller
// decoder makes cuvidDecodePicture fail with CUDA_ERROR_INVALID_VALUE as
// soon as the stream needs more reference surfaces than we allocated.
    bool flushed = false;
    bool failed = false;

    static int CUDAAPI sequenceCallback(void* user, CUVIDEOFORMAT* format);
    static int CUDAAPI decodeCallback(void* user, CUVIDPICPARAMS* params);
    static int CUDAAPI displayCallback(void* user, CUVIDPARSERDISPINFO* info);

    void waitSlotReady(uint32_t index) {
        auto& [dev_ptr, pitch, bytes, in_use, export_evt, export_pending] = slots[index];
        (void)dev_ptr; (void)pitch; (void)bytes; (void)in_use;
        if (!export_pending || export_evt == nullptr) return;
        if (rtx_shim::cuda::cudaCtxDriver(cuda).cuEventSynchronize != nullptr) {
            (void)rtx_shim::cuda::cudaCtxDriver(cuda).cuEventSynchronize(export_evt);
        }
    }
    RtxResult fail(RtxResult result, const std::string& message) {
        last_error = message;
        failed = true;
        return result;
    }
    bool ensureSlots(int frame_width, int frame_height) {
        if (frame_width <= 0 || frame_height <= 0) {
            return false;
        }
        if (!slots.empty() && frame_width == width && frame_height == height) {
            return true;
        }
        if (stream != nullptr && rtx_shim::cuda::cudaCtxDriver(cuda).cuStreamSynchronize != nullptr) {
            (void)rtx_shim::cuda::cudaCtxDriver(cuda).cuStreamSynchronize(stream);
        }
        ready.clear();
        for (auto& [dev_ptr, pitch, bytes, in_use, export_evt, export_pending] : slots) {
            (void)pitch; (void)bytes; (void)in_use;
            if (export_evt != nullptr && rtx_shim::cuda::cudaCtxDriver(cuda).cuEventDestroy != nullptr) {
                (void)rtx_shim::cuda::cudaCtxDriver(cuda).cuEventDestroy(export_evt);
                export_evt = nullptr;
            }
            export_pending = false;
            if (dev_ptr != 0) {
                rtx_shim::cuda::cudaCtxDriver(cuda).cuMemFree(dev_ptr);
                dev_ptr = 0;
            }
        }
        slots.clear();
        free_slots.clear();
        width = frame_width;
        height = frame_height;

        const size_t sample_bytes = bit_depth > 8 ? 2u : 1u;
        const uint32_t wanted =
            config.pool_slots > 0 ? static_cast<uint32_t>(std::min(config.pool_slots, 64)) : 16u;
        slots.resize(wanted);
        for (uint32_t i = 0; i < wanted; ++i) {
            auto& [dev_ptr, pitch, bytes, in_use, export_evt, export_pending] = slots[i];
            (void)export_evt; (void)export_pending;
            auto result = rtx_shim::cuda::cudaCtxDriver(cuda).cuMemAllocPitch(
                &dev_ptr, &pitch,
                static_cast<size_t>(width) * sample_bytes,
                static_cast<size_t>(height) * 3 / 2, 16);
            if (result != CUDA_SUCCESS) {
                last_error = "cuMemAllocPitch failed for decode pool";
                return false;
            }
            bytes = pitch * static_cast<size_t>(height) * 3 / 2;
            in_use = false;
            free_slots.push_back(i);
        }
        return true;
    }
    int onSequence(CUVIDEOFORMAT* format) {
        if (format == nullptr) {
            last_error = "CUVID sequence callback received null format";
            return 0;
        }
        bit_depth = static_cast<int>(format->bit_depth_luma_minus8) + 8;
        if (format->chroma_format != cudaVideoChromaFormat_420) {
            last_error = "stage 1 supports 4:2:0 sources only (stream chroma format " +
                         std::to_string(static_cast<int>(format->chroma_format)) + ")";
            failed = true;
            return 0;
        }
// The stage-1 pool and the NVENC input formats are 4:2:0 only.
        const int coded_width = static_cast<int>(format->coded_width);
        const int coded_height = static_cast<int>(format->coded_height);
        int display_width = static_cast<int>(format->display_area.right);
        int display_height = static_cast<int>(format->display_area.bottom);
        if (display_width <= 0 || display_width > coded_width) {
            display_width = coded_width;
        }
// Container geometry is the display area (e.g. 1920x1080), while the
// coded size is macroblock aligned (1920x1088).  The decode pool and the
// NVENC session must use the display geometry, otherwise the muxer
// declares 1080 lines for a 1088-line bitstream.
        if (display_height <= 0 || display_height > coded_height) {
            display_height = coded_height;
        }
        display_width &= ~1;  // 4:2:0 requires an even width
        display_height &= ~1;
        const bool geometry_changed =
            (!slots.empty() && (display_width != width || display_height != height));
        if (!ensureSlots(display_width, display_height)) {
            return 0;
        }
        if (decoder != nullptr) {
            if (!geometry_changed) {
                return 1;
            }
            destroyDecoder(decoder);
            decoder = nullptr;
        }
        if (format->min_num_decode_surfaces > surface_count) {
            last_error = "stream needs " + std::to_string(format->min_num_decode_surfaces) +
                         " decode surfaces but only " + std::to_string(surface_count) +
                         " were configured; raise RtxDecoderConfig::max_surfaces";
            failed = true;
            return 0;
        }
        CUVIDDECODECREATEINFO create{};
        create.CodecType = format->codec;
        create.ChromaFormat = format->chroma_format;
        create.ulWidth = static_cast<unsigned long>(coded_width);
        create.ulHeight = static_cast<unsigned long>(coded_height);
        create.ulNumDecodeSurfaces = surface_count;
        create.ulTargetWidth = static_cast<unsigned long>(display_width);
        create.ulTargetHeight = static_cast<unsigned long>(display_height);
        create.ulMaxWidth = static_cast<unsigned long>(coded_width);
        create.ulMaxHeight = static_cast<unsigned long>(coded_height);
        create.bitDepthMinus8 = static_cast<unsigned long>(bit_depth > 8 ? bit_depth - 8 : 0);
        create.OutputFormat = bit_depth > 8 ? cudaVideoSurfaceFormat_P016
                                            : cudaVideoSurfaceFormat_NV12;
        create.ulNumOutputSurfaces = static_cast<unsigned long>(
            std::max<unsigned long>(static_cast<unsigned long>(slots.size()), 8ul));
        create.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
        create.target_rect.left = 0;
        create.target_rect.top = 0;
        create.target_rect.right = static_cast<short>(display_width);
// The export copy is asynchronous (ACT-04): the parser thread enqueues
// cuMemcpy2DAsync and unmaps immediately, so several decoded frames can
// be in flight at once.  The output surface pool must cover the whole
// decode ring, otherwise cuvidMapVideoFrame64 fails with
// CUDA_ERROR_NOT_MAPPED (205) once the third concurrent frame arrives.
// ulNumOutputSurfaces is the pool size, not the DPB size; matching it
// to the slot ring (floor of 8) keeps every display callback mappable.
        create.target_rect.bottom = static_cast<short>(display_height);

        const CUresult result = createDecoder(&decoder, &create);
        RTX_TRACE("decoder: cuvidCreateDecoder %dx%d (coded %dx%d, %d-bit) -> %d", display_width,
                  display_height, coded_width, coded_height, bit_depth,
                  static_cast<int>(result));
        if (result != CUDA_SUCCESS) {
            last_error = "cuvidCreateDecoder failed (CUDA error " +
                         std::to_string(static_cast<int>(result)) +
                         "); the GPU may not provide an NVDEC engine for this codec";
            failed = true;
            return 0;
        }
        if (stream == nullptr) {
            if (rtx_shim::cuda::cudaCtxDriver(cuda).cuStreamCreate == nullptr) {
                last_error = "cuStreamCreate is unavailable";
// Surface the failure through every subsequent entry point instead
// of letting cuvidParseVideoData retry a session that cannot exist.
                return 0;
            }
            void* raw_stream = nullptr;
            if (rtx_shim::cuda::cudaCtxDriver(cuda).cuStreamCreate(&raw_stream, 0) != CUDA_SUCCESS) {
                last_error = "cuStreamCreate failed";
                return 0;
            }
            stream = static_cast<CUstream>(raw_stream);
        }
        return 1;
    }
    int onDecode(CUVIDPICPARAMS* params) {
        if (decoder == nullptr || params == nullptr) {
            last_error = "cuvidDecodePicture called without a decoder";
            return 0;
        }
        const CUresult result = decodePicture(decoder, params);
        if (result != CUDA_SUCCESS) {
            last_error = "cuvidDecodePicture failed (CUDA error " +
                         std::to_string(static_cast<int>(result)) + ")";
            RTX_TRACE("decoder: cuvidDecodePicture -> %d (pic idx %d, %u slices)",
                      static_cast<int>(result), params->CurrPicIdx, params->nNumSlices);
            failed = true;
            return 0;
        }
        return 1;
    }
    int onDisplay(CUVIDPARSERDISPINFO* info) {
        if (decoder == nullptr || info == nullptr) {
            return 1;
        }
        if (info->picture_index < 0) {
            return 1;
        }
        RTX_TRACE("decoder: display callback picture_index=%d ts=%lld", info->picture_index,
                  static_cast<long long>(info->timestamp));
        if (free_slots.empty()) {
            last_error = "decode pool exhausted; release frames before submitting more";
            RTX_TRACE("decoder: display callback rejected - decode pool exhausted");
// CUVID_PKT_NOTIFY_EOS delivers a sentinel display callback with a
// negative picture index; it is not a decoded surface.
            return 0;
        }
        const uint32_t slot_index = free_slots.front();
        free_slots.pop_front();
        auto& slot = slots[slot_index];

        CUdeviceptr source = 0;
        unsigned int source_pitch = 0;
        CUVIDPROCPARAMS process{};
        process.progressive_frame = info->progressive_frame;
// Bound the number of NVDEC surfaces kept mapped for the asynchronous
// export.  CUVID recycles its DPB indices; if a surface is still mapped
// when the parser comes back around, cuvidMapVideoFrame64 fails with
// CUDA_ERROR_MAP_FAILED (205).  Keeping the outstanding set strictly
// below the surface count makes that impossible, and the wait is almost
// always already satisfied because the copy was issued frames ago.
        process.top_field_first = info->top_field_first;
        process.second_field = 0;
        process.unpaired_field = 0;
        process.output_stream = stream;

        RTX_TRACE("decoder: map picture=%d slot=%u", info->picture_index, slot_index);
        int result = static_cast<int>(
            mapVideoFrame(decoder, info->picture_index, &source, &source_pitch,
                                    &process));
        if (result != CUDA_SUCCESS) {
            last_error = "cuvidMapVideoFrame64 failed";
            RTX_TRACE("decoder: cuvidMapVideoFrame64 -> %d", result);
            failed = true;
            free_slots.push_front(slot_index);
            return 0;
        }
        const size_t sample_bytes = bit_depth > 8 ? 2u : 1u;
        const size_t plane_width = static_cast<size_t>(width) * sample_bytes;
        DriverTable& driver = rtx_shim::cuda::cudaCtxDriver(cuda);

        auto& [slot_ptr, slot_pitch, slot_bytes, slot_in_use, slot_evt, slot_pending] = slot;
        (void)slot_bytes;
        rtx_shim::cuda::CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_DEVICE;
        copy.srcDevice = source;
        copy.srcPitch = source_pitch;
        copy.dstMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_DEVICE;
        copy.dstDevice = slot_ptr;
        copy.dstPitch = slot_pitch;
        copy.WidthInBytes = plane_width;
        copy.Height = static_cast<size_t>(height);
        result = driver.cuMemcpy2DAsync(&copy, stream);
        if (result == CUDA_SUCCESS) {
            rtx_shim::cuda::CUDA_MEMCPY2D chroma = copy;
            chroma.srcDevice = source + static_cast<CUdeviceptr>(source_pitch) *
                                            static_cast<CUdeviceptr>(height);
            chroma.dstDevice = slot_ptr + static_cast<CUdeviceptr>(slot_pitch) *
                                              static_cast<CUdeviceptr>(height);
            chroma.Height = static_cast<size_t>(height) / 2;
            result = driver.cuMemcpy2DAsync(&chroma, stream);
        }
        if (result == CUDA_SUCCESS) {
            if (slot_evt == nullptr && driver.cuEventCreate != nullptr) {
                if (driver.cuEventCreate(&slot_evt, CU_EVENT_DISABLE_TIMING) != CUDA_SUCCESS) {
                    slot_evt = nullptr;
                }
            }
            if (slot_evt != nullptr && driver.cuEventRecord != nullptr &&
                driver.cuEventRecord(slot_evt, stream) == CUDA_SUCCESS) {
                slot_pending = true;
            } else if (driver.cuStreamSynchronize != nullptr) {
                result = driver.cuStreamSynchronize(stream);
                slot_pending = false;
            }
        }
        unmapVideoFrame(decoder, source);
        if (result != CUDA_SUCCESS) {
            last_error = "cuMemcpy2DAsync failed while exporting a decoded surface";
            free_slots.push_front(slot_index);
            return 0;
        }
        slot_in_use = true;
        RtxFrameDesc frame{};
        frame.planes[0] = slot_ptr;
        frame.planes[1] = slot_ptr + static_cast<CUdeviceptr>(slot_pitch) *
                                         static_cast<CUdeviceptr>(height);
        frame.pitches[0] = static_cast<uint32_t>(slot_pitch);
        frame.pitches[1] = static_cast<uint32_t>(slot_pitch);
        frame.width = width;
        frame.height = height;
        frame.format = bit_depth > 8 ? RTX_PIXEL_P010 : RTX_PIXEL_NV12;
        frame.bit_depth = bit_depth;
        frame.pts = static_cast<int64_t>(info->timestamp);
        frame.slot_index = slot_index;
        ready.push_back(frame);
        ++stats.frames_out;
        stats.device_to_device_bytes += plane_width * static_cast<size_t>(height) * 3 / 2;
        return 1;
    }
};

int CUDAAPI RtxDecoder::sequenceCallback(void* user, CUVIDEOFORMAT* format) {
    return static_cast<RtxDecoder*>(user)->onSequence(format);
}
int CUDAAPI RtxDecoder::decodeCallback(void* user, CUVIDPICPARAMS* params) {
    return static_cast<RtxDecoder*>(user)->onDecode(params);
}
int CUDAAPI RtxDecoder::displayCallback(void* user, CUVIDPARSERDISPINFO* info) {
    return static_cast<RtxDecoder*>(user)->onDisplay(info);
}
extern "C" {

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_create(const RtxDecoderConfig* config, RtxDecoder** decoder) {
    if (decoder == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *decoder = nullptr;
    if (config == nullptr || config->codec == RTX_CODEC_UNKNOWN || config->width <= 0 ||
        config->height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (mapCodec(config->codec) == cudaVideoCodec_NumCodecs) {
        return RTX_ERR_UNSUPPORTED;
    }
    auto* self = new (std::nothrow) RtxDecoder();
    if (self == nullptr) {
        return RTX_ERR_OUT_OF_MEMORY;
    }
    self->config = *config;
    self->bit_depth = config->bit_depth > 8 ? 10 : 8;
    self->surface_count = static_cast<unsigned int>(
        std::clamp(config->max_surfaces > 0 ? config->max_surfaces : 32, 8, 64));

    std::string error;
    if (!rtx_shim::cuda::cudaCtxOpen(self->cuda, &error, config->device_index)) {
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    if (!rtx_shim::openNvidiaLibrary(self->cuvid, "nvcuvid.dll", "libnvcuvid.so.1", true,
                                     &error)) {
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    if (!self->resolveFunctions(self->cuvid, &error)) {
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    if (self->getDecoderCaps != nullptr) {
        CUVIDDECODECAPS caps{};
        caps.eCodecType = mapCodec(config->codec);
        caps.eChromaFormat = cudaVideoChromaFormat_420;
        caps.nBitDepthMinus8 = static_cast<unsigned int>(self->bit_depth > 8 ? 2 : 0);
        const CUresult caps_result = self->getDecoderCaps(&caps);
        RTX_TRACE("decoder: cuvidGetDecoderCaps codec=%d %d-bit -> %d supported=%d max=%ux%u",
                  config->codec, self->bit_depth, static_cast<int>(caps_result),
                  static_cast<int>(caps.bIsSupported), caps.nMaxWidth, caps.nMaxHeight);
        if (caps_result == CUDA_SUCCESS && caps.bIsSupported == 0) {
            delete self;
            return RTX_ERR_ARCH_UNSUPPORTED;
        }
    }
// Query the NVDEC capability matrix before building a parser.  Maxwell
// GM204, for example, has no HEVC decode engine: cuvidCreateDecoder would
// only fail later, inside the first sequence callback.
    CUVIDPARSERPARAMS params{};
    params.CodecType = mapCodec(config->codec);
    params.ulMaxNumDecodeSurfaces = self->surface_count;
    params.ulClockRate = static_cast<unsigned int>(config->clock_rate > 0 ? config->clock_rate
                                                                         : 90000);
    params.ulErrorThreshold = 0;
    params.ulMaxDisplayDelay = 4;
    params.pUserData = self;
    params.pfnSequenceCallback = &RtxDecoder::sequenceCallback;
    params.pfnDecodePicture = &RtxDecoder::decodeCallback;
    params.pfnDisplayPicture = &RtxDecoder::displayCallback;
    const CUresult result = self->createParser(&self->parser, &params);
    RTX_TRACE("decoder: cuvidCreateVideoParser codec=%d -> %d", config->codec,
              static_cast<int>(result));
    if (result != CUDA_SUCCESS || self->parser == nullptr) {
        delete self;
        return mapResult(result == CUDA_SUCCESS ? CUDA_ERROR_INVALID_VALUE : result);
    }
    *decoder = self;
    return RTX_OK;
}
RTX_SHIM_API void RTX_SHIM_CALL rtx_shim_decoder_destroy(RtxDecoder* decoder) {
    if (decoder == nullptr) {
        return;
    }
    if (decoder->parser != nullptr) {
        decoder->destroyParser(decoder->parser);
        decoder->parser = nullptr;
    }
    if (decoder->decoder != nullptr) {
        decoder->destroyDecoder(decoder->decoder);
        decoder->decoder = nullptr;
    }
    if (decoder->stream != nullptr) {
        rtx_shim::cuda::cudaCtxDriver(decoder->cuda).cuStreamDestroy(decoder->stream);
        decoder->stream = nullptr;
    }
    for (auto& [dev_ptr, pitch, bytes, in_use, export_evt, export_pending] : decoder->slots) {
        (void)pitch; (void)bytes; (void)in_use; (void)export_pending;
        if (export_evt != nullptr && rtx_shim::cuda::cudaCtxDriver(decoder->cuda).cuEventDestroy != nullptr) {
            rtx_shim::cuda::cudaCtxDriver(decoder->cuda).cuEventDestroy(export_evt);
            export_evt = nullptr;
        }
        if (dev_ptr != 0) {
            rtx_shim::cuda::cudaCtxDriver(decoder->cuda).cuMemFree(dev_ptr);
            dev_ptr = 0;
        }
    }
    delete decoder;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_submit(RtxDecoder* decoder, const uint8_t* data, size_t size, int64_t pts,
                        int64_t /*dts*/, int32_t flags) {
    if (decoder == nullptr || (data == nullptr && size != 0)) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    (void)flags;  /* keyframe hint: the CUVID parser derives picture boundaries */
    if (decoder->failed) {
        return RTX_ERR_INTERNAL;
    }
    if (decoder->end_of_stream) {
        return RTX_ERR_IO;
    }
    CUVIDSOURCEDATAPACKET packet{};
    packet.flags = CUVID_PKT_TIMESTAMP;
    packet.payload_size = static_cast<tcu_ulong>(size);
    packet.payload = data;
    packet.timestamp = static_cast<CUvideotimestamp>(pts);
    const CUresult result = decoder->parseVideoData(decoder->parser, &packet);
    RTX_TRACE("decoder: cuvidParseVideoData %zu bytes pts=%lld -> %d", size,
              static_cast<long long>(pts), static_cast<int>(result));
    if (result != CUDA_SUCCESS) {
        return decoder->fail(mapResult(result), "cuvidParseVideoData failed");
    }
    ++decoder->stats.frames_in;
    return RTX_OK;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_receive(RtxDecoder* decoder, RtxFrameDesc* frame) {
    if (decoder == nullptr || frame == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (!decoder->ready.empty()) {
        decoder->waitSlotReady(static_cast<uint32_t>(decoder->ready.front().slot_index));
        *frame = decoder->ready.front();
        decoder->ready.pop_front();
        return RTX_OK;
    }
    if (decoder->failed) {
        return RTX_ERR_INTERNAL;
    }
    if (decoder->end_of_stream) {
        return RTX_ERR_IO;
    }
    return RTX_ERR_NOT_FOUND;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_decoder_flush(RtxDecoder* decoder) {    if (decoder == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (decoder->failed) {
// Opportunistically retire export copies that have already completed.
        return RTX_ERR_INTERNAL;
// The encoder reads the surface on its own engine, so the export copy
// for this specific slot must have finished before we hand it over.
    }
    if (decoder->end_of_stream) {
        return RTX_OK;
    }
    decoder->end_of_stream = true;
    CUVIDSOURCEDATAPACKET packet{};
    packet.flags = CUVID_PKT_ENDOFSTREAM;
    packet.payload = nullptr;
    packet.payload_size = 0;
    packet.timestamp = 0;
    RTX_TRACE("decoder: cuvidParseVideoData(end-of-stream) submitting");
    const CUresult result = decoder->parseVideoData(decoder->parser, &packet);
    RTX_TRACE("decoder: cuvidParseVideoData(end-of-stream) -> %d", static_cast<int>(result));
    if (result != CUDA_SUCCESS) {
        return decoder->fail(mapResult(result), "cuvidParseVideoData(end-of-stream) failed");
    }
    return RTX_OK;
}
// A failed sequence/decode callback leaves the CUVID parser in an
// undefined state; submitting EOS can block indefinitely.
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_decoder_reset(RtxDecoder* decoder) {
    if (decoder == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (decoder->failed) {
        return RTX_ERR_INTERNAL;
    }
// CUVID_PKT_NOTIFY_EOS makes the parser deliver a sentinel display
// callback; CUVID_PKT_ENDOFSTREAM alone drains the DPB and returns, which
// is what a synchronous transcode loop needs.
    DriverTable& driver = rtx_shim::cuda::cudaCtxDriver(decoder->cuda);
    if (decoder->stream != nullptr && driver.cuStreamSynchronize != nullptr) {
        const int synced = driver.cuStreamSynchronize(decoder->stream);
        if (synced != rtx_shim::cuda::CUDA_SUCCESS) {
            return decoder->fail(mapResult(static_cast<CUresult>(synced)),
                                 "cuStreamSynchronize failed for the non-terminal reset");
        }
    }
    decoder->ready.clear();
    decoder->free_slots.clear();
    for (uint32_t index = 0; index < decoder->slots.size(); ++index) {
        auto& [dp, pt, by, in_use, ev, ep] = decoder->slots[index];
        (void)dp; (void)pt; (void)by; (void)ev; (void)ep;
        in_use = false;
        decoder->free_slots.push_back(index);
    }
    decoder->end_of_stream = false;
    decoder->flushed = false;
    CUVIDSOURCEDATAPACKET packet{};
    packet.flags = CUVID_PKT_DISCONTINUITY;
// 与终态 flush 同一裁决：回调失败后的解析器状态未定义，复位同样拒绝。
    packet.payload = nullptr;
    packet.payload_size = 0;
    packet.timestamp = 0;
// 1. 同步 stream：前序异步导出拷贝（slot -> NVENC/消费方）必须先安全落盘，
//    之后槽位才能立刻复用。
    RTX_TRACE("decoder: cuvidParseVideoData(discontinuity) submitting");
    const CUresult result = decoder->parseVideoData(decoder->parser, &packet);
    RTX_TRACE("decoder: cuvidParseVideoData(discontinuity) -> %d", static_cast<int>(result));
    if (result != CUDA_SUCCESS) {
        return decoder->fail(mapResult(result),
                             "cuvidParseVideoData(discontinuity) failed");
    }
// 2. 清空 ready 队列，全部槽位复原为 in_use = false 并归还 free_slots。
    return RTX_OK;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_release_frame(RtxDecoder* decoder, uint64_t slot_index) {
    if (decoder == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
// 3. 非终态：清零 end_of_stream 与 flushed，submit/receive 立即可用。
    }
    if (slot_index >= decoder->slots.size()) {
// 4. 提交 CUVID_PKT_DISCONTINUITY（0x04）空包：解析器据此复位参考帧状态，
//    下一个 IDR 之前的包按新序列重建参考。与 flush 的 ENDOFSTREAM 不同，
//    解码器实例与 CUDA 主上下文引用计数原封不动，GOP Seek 不再重建会话。
        return RTX_ERR_INVALID_ARGUMENT;
    }
    auto& [dp, pt, by, in_use, ev, ep] = decoder->slots[static_cast<size_t>(slot_index)];
    (void)dp; (void)pt; (void)by; (void)ev; (void)ep;
    if (!in_use) return RTX_ERR_INVALID_ARGUMENT;
    in_use = false;
    decoder->free_slots.push_back(static_cast<uint32_t>(slot_index));
    return RTX_OK;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_download(RtxDecoder* decoder, const RtxFrameDesc* frame, uint8_t* destination,
                          size_t destination_pitch) {
    if (decoder == nullptr || frame == nullptr || destination == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (frame->slot_index >= decoder->slots.size()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    const auto& [slot_ptr, slot_pitch, slot_bytes, slot_in_use, slot_evt, slot_pending] =
        decoder->slots[static_cast<size_t>(frame->slot_index)];
    (void)slot_bytes; (void)slot_evt; (void)slot_pending;
    if (!slot_in_use) return RTX_ERR_INVALID_ARGUMENT;
    if (frame->width <= 0 || frame->height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    const size_t sample_bytes = frame->format == RTX_PIXEL_P010 ? 2u : 1u;
    const size_t row_bytes = static_cast<size_t>(frame->width) * sample_bytes;
    if (destination_pitch == 0) {
        destination_pitch = row_bytes;
    }
    if (destination_pitch < row_bytes) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    DriverTable& driver = rtx_shim::cuda::cudaCtxDriver(decoder->cuda);
    rtx_shim::cuda::CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = slot_ptr;
    copy.srcPitch = slot_pitch;
    copy.dstMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_HOST;
    copy.dstHost = destination;
    copy.dstPitch = destination_pitch;
    copy.WidthInBytes = row_bytes;
    copy.Height = static_cast<size_t>(frame->height);
    int result = driver.cuMemcpy2DAsync(&copy, nullptr);
    if (result == rtx_shim::cuda::CUDA_SUCCESS) {
        rtx_shim::cuda::CUDA_MEMCPY2D chroma = copy;
        chroma.srcDevice = slot_ptr + static_cast<CUdeviceptr>(slot_pitch) *
                                             static_cast<CUdeviceptr>(frame->height);
        chroma.dstHost = destination + static_cast<size_t>(destination_pitch) *
                                            static_cast<size_t>(frame->height);
        chroma.Height = static_cast<size_t>(frame->height) / 2;
        result = driver.cuMemcpy2DAsync(&chroma, nullptr);
    }
    if (result == rtx_shim::cuda::CUDA_SUCCESS && driver.cuStreamSynchronize != nullptr) {
        result = driver.cuStreamSynchronize(nullptr);
    }
    if (result != rtx_shim::cuda::CUDA_SUCCESS) {
        return RTX_ERR_INTERNAL;
    }
    decoder->stats.device_to_host_pixel_bytes += row_bytes * static_cast<size_t>(frame->height) * 3 / 2;
    ++decoder->stats.host_pixel_copies;
    return RTX_OK;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_get_stats(RtxDecoder* decoder, RtxHwStats* stats) {
    if (decoder == nullptr || stats == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *stats = decoder->stats;
    return RTX_OK;
}
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_decoder_last_error(RtxDecoder* decoder) {
    if (decoder == nullptr) {
        return "invalid RtxDecoder handle";
    }
    return decoder->last_error.c_str();
}
}  // extern "C"
