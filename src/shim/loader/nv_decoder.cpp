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

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "ffnvcodec/dynlink_cuda.h"
#include "ffnvcodec/dynlink_nvcuvid.h"

#include "dyn_library.h"
#include "nv_cuda_context.h"
#include "nv_func_tables.h"
#include "nv_trace.h"

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

struct DecoderFunctions {
    tcuvidCreateVideoParser* createParser = nullptr;
    tcuvidParseVideoData* parseVideoData = nullptr;
    tcuvidDestroyVideoParser* destroyParser = nullptr;
    tcuvidCreateDecoder* createDecoder = nullptr;
    tcuvidDestroyDecoder* destroyDecoder = nullptr;
    tcuvidDecodePicture* decodePicture = nullptr;
    tcuvidMapVideoFrame64* mapVideoFrame = nullptr;
    tcuvidUnmapVideoFrame64* unmapVideoFrame = nullptr;
    tcuvidGetDecoderCaps* getDecoderCaps = nullptr;

    bool resolve(const DynLibrary& library, std::string* error) {
        auto load = [&](const char* name) -> void* { return library.symbol(name); };
        createParser = reinterpret_cast<tcuvidCreateVideoParser*>(load("cuvidCreateVideoParser"));
        parseVideoData = reinterpret_cast<tcuvidParseVideoData*>(load("cuvidParseVideoData"));
        destroyParser = reinterpret_cast<tcuvidDestroyVideoParser*>(load("cuvidDestroyVideoParser"));
        createDecoder = reinterpret_cast<tcuvidCreateDecoder*>(load("cuvidCreateDecoder"));
        destroyDecoder = reinterpret_cast<tcuvidDestroyDecoder*>(load("cuvidDestroyDecoder"));
        decodePicture = reinterpret_cast<tcuvidDecodePicture*>(load("cuvidDecodePicture"));
        mapVideoFrame = reinterpret_cast<tcuvidMapVideoFrame64*>(load("cuvidMapVideoFrame64"));
        unmapVideoFrame = reinterpret_cast<tcuvidUnmapVideoFrame64*>(load("cuvidUnmapVideoFrame64"));
        getDecoderCaps = reinterpret_cast<tcuvidGetDecoderCaps*>(load("cuvidGetDecoderCaps"));
        if (createParser == nullptr || parseVideoData == nullptr || destroyParser == nullptr ||
            createDecoder == nullptr || destroyDecoder == nullptr || decodePicture == nullptr ||
            mapVideoFrame == nullptr || unmapVideoFrame == nullptr) {
            if (error != nullptr) {
                *error = "nvcuvid is missing one or more cuvid entry points";
            }
            return false;
        }
        return true;
    }
};

struct Slot {
    CUdeviceptr pointer = 0;
    size_t pitch = 0;
    size_t bytes = 0;
    bool in_use = false;
    // Completion event for the export copy.  The surface is unmapped as soon as
    // the copy is enqueued (the driver keeps it alive until the stream work
    // completes), and the consumer waits on this event before handing the
    // buffer to NVENC.
    // DriverTable declares its own opaque CUevent (void*) so that the shim does
    // not depend on the global cuda.h typedef; keep the member in that type.
    rtx_shim::cuda::CUevent ready_event = nullptr;
    bool event_valid = false;
};

}  // namespace

struct RtxDecoder {
    RtxDecoderConfig config{};
    Context cuda;
    DynLibrary cuvid;
    DecoderFunctions functions;
    CUvideoparser parser = nullptr;
    CUvideodecoder decoder = nullptr;
    CUstream stream = nullptr;
    std::vector<Slot> slots;
    std::deque<uint32_t> free_slots;
    std::deque<RtxFrameDesc> ready;
    RtxHwStats stats{};
    std::string last_error;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    // The CUVID parser hands out CurrPicIdx values from its own surface space,
    // so the decoder must be created with exactly the same count.  A smaller
    // decoder makes cuvidDecodePicture fail with CUDA_ERROR_INVALID_VALUE as
    // soon as the stream needs more reference surfaces than we allocated.
    unsigned int surface_count = 32;
    bool end_of_stream = false;
    bool flushed = false;
    bool failed = false;

    static int CUDAAPI sequenceCallback(void* user, CUVIDEOFORMAT* format);
    static int CUDAAPI decodeCallback(void* user, CUVIDPICPARAMS* params);
    static int CUDAAPI displayCallback(void* user, CUVIDPARSERDISPINFO* info);

    // Guarantees that a frame handed to the encoder is fully copied.  NVENC
    // consumes the pointer on its own engine, so CUDA stream ordering alone is
    // not enough; the export event must have signalled.
    void waitSlotReady(uint32_t index) {
        Slot& slot = slots[index];
        if (!slot.event_valid || slot.ready_event == nullptr) {
            return;
        }
        if (cuda.driver().cuEventSynchronize != nullptr) {
            (void)cuda.driver().cuEventSynchronize(slot.ready_event);
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
        if (stream != nullptr && cuda.driver().cuStreamSynchronize != nullptr) {
            (void)cuda.driver().cuStreamSynchronize(stream);
        }
        // Format change or first frame: rebuild the pool.  Frames still queued
        // for the encoder belong to the old geometry and are invalidated.
        ready.clear();
        for (Slot& slot : slots) {
            if (slot.ready_event != nullptr && cuda.driver().cuEventDestroy != nullptr) {
                (void)cuda.driver().cuEventDestroy(slot.ready_event);
                slot.ready_event = nullptr;
            }
            slot.event_valid = false;
            if (slot.pointer != 0) {
                cuda.driver().cuMemFree(slot.pointer);
                slot.pointer = 0;
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
            Slot& slot = slots[i];
            auto result = cuda.driver().cuMemAllocPitch(
                &slot.pointer, &slot.pitch,
                static_cast<size_t>(width) * sample_bytes,
                static_cast<size_t>(height) * 3 / 2, 16);
            if (result != CUDA_SUCCESS) {
                last_error = "cuMemAllocPitch failed for decode pool";
                return false;
            }
            slot.bytes = slot.pitch * static_cast<size_t>(height) * 3 / 2;
            slot.in_use = false;
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
            // The stage-1 pool and the NVENC input formats are 4:2:0 only.
            last_error = "stage 1 supports 4:2:0 sources only (stream chroma format " +
                         std::to_string(static_cast<int>(format->chroma_format)) + ")";
            failed = true;
            return 0;
        }
        const int coded_width = static_cast<int>(format->coded_width);
        const int coded_height = static_cast<int>(format->coded_height);
        // Container geometry is the display area (e.g. 1920x1080), while the
        // coded size is macroblock aligned (1920x1088).  The decode pool and the
        // NVENC session must use the display geometry, otherwise the muxer
        // declares 1080 lines for a 1088-line bitstream.
        int display_width = static_cast<int>(format->display_area.right);
        int display_height = static_cast<int>(format->display_area.bottom);
        if (display_width <= 0 || display_width > coded_width) {
            display_width = coded_width;
        }
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
            functions.destroyDecoder(decoder);
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
        // The export copy is asynchronous (ACT-04): the parser thread enqueues
        // cuMemcpy2DAsync and unmaps immediately, so several decoded frames can
        // be in flight at once.  The output surface pool must cover the whole
        // decode ring, otherwise cuvidMapVideoFrame64 fails with
        // CUDA_ERROR_NOT_MAPPED (205) once the third concurrent frame arrives.
        // ulNumOutputSurfaces is the pool size, not the DPB size; matching it
        // to the slot ring (floor of 8) keeps every display callback mappable.
        create.ulNumOutputSurfaces = static_cast<unsigned long>(
            std::max<unsigned long>(static_cast<unsigned long>(slots.size()), 8ul));
        create.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
        create.target_rect.left = 0;
        create.target_rect.top = 0;
        create.target_rect.right = static_cast<short>(display_width);
        create.target_rect.bottom = static_cast<short>(display_height);

        const CUresult result = functions.createDecoder(&decoder, &create);
        RTX_TRACE("decoder: cuvidCreateDecoder %dx%d (coded %dx%d, %d-bit) -> %d", display_width,
                  display_height, coded_width, coded_height, bit_depth,
                  static_cast<int>(result));
        if (result != CUDA_SUCCESS) {
            last_error = "cuvidCreateDecoder failed (CUDA error " +
                         std::to_string(static_cast<int>(result)) +
                         "); the GPU may not provide an NVDEC engine for this codec";
            // Surface the failure through every subsequent entry point instead
            // of letting cuvidParseVideoData retry a session that cannot exist.
            failed = true;
            return 0;
        }
        if (stream == nullptr) {
            if (cuda.driver().cuStreamCreate == nullptr) {
                last_error = "cuStreamCreate is unavailable";
                return 0;
            }
            void* raw_stream = nullptr;
            if (cuda.driver().cuStreamCreate(&raw_stream, 0) != CUDA_SUCCESS) {
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
        const CUresult result = functions.decodePicture(decoder, params);
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
        // CUVID_PKT_NOTIFY_EOS delivers a sentinel display callback with a
        // negative picture index; it is not a decoded surface.
        if (info->picture_index < 0) {
            return 1;
        }
        RTX_TRACE("decoder: display callback picture_index=%d ts=%lld", info->picture_index,
                  static_cast<long long>(info->timestamp));
        if (free_slots.empty()) {
            last_error = "decode pool exhausted; release frames before submitting more";
            RTX_TRACE("decoder: display callback rejected - decode pool exhausted");
            return 0;
        }
        // Bound the number of NVDEC surfaces kept mapped for the asynchronous
        // export.  CUVID recycles its DPB indices; if a surface is still mapped
        // when the parser comes back around, cuvidMapVideoFrame64 fails with
        // CUDA_ERROR_MAP_FAILED (205).  Keeping the outstanding set strictly
        // below the surface count makes that impossible, and the wait is almost
        // always already satisfied because the copy was issued frames ago.
        const uint32_t slot_index = free_slots.front();
        free_slots.pop_front();
        Slot& slot = slots[slot_index];

        CUdeviceptr source = 0;
        unsigned int source_pitch = 0;
        CUVIDPROCPARAMS process{};
        process.progressive_frame = info->progressive_frame;
        process.top_field_first = info->top_field_first;
        process.second_field = 0;
        process.unpaired_field = 0;
        process.output_stream = stream;

        RTX_TRACE("decoder: map picture=%d slot=%u", info->picture_index, slot_index);
        int result = static_cast<int>(
            functions.mapVideoFrame(decoder, info->picture_index, &source, &source_pitch,
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
        DriverTable& driver = cuda.driver();

        rtx_shim::cuda::CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_DEVICE;
        copy.srcDevice = source;
        copy.srcPitch = source_pitch;
        copy.dstMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_DEVICE;
        copy.dstDevice = slot.pointer;
        copy.dstPitch = slot.pitch;
        copy.WidthInBytes = plane_width;
        copy.Height = static_cast<size_t>(height);
        result = driver.cuMemcpy2DAsync(&copy, stream);
        if (result == CUDA_SUCCESS) {
            rtx_shim::cuda::CUDA_MEMCPY2D chroma = copy;
            chroma.srcDevice = source + static_cast<CUdeviceptr>(source_pitch) *
                                            static_cast<CUdeviceptr>(height);
            chroma.dstDevice = slot.pointer + static_cast<CUdeviceptr>(slot.pitch) *
                                               static_cast<CUdeviceptr>(height);
            chroma.Height = static_cast<size_t>(height) / 2;
            result = driver.cuMemcpy2DAsync(&chroma, stream);
        }
        // Record completion instead of blocking the parser thread, then unmap
        // right away: the driver keeps the surface alive until the enqueued copy
        // retires, which is the pattern cuvid clients use.  The consumer waits
        // on the event before handing the buffer to NVENC.
        if (result == CUDA_SUCCESS) {
            if (slot.ready_event == nullptr && driver.cuEventCreate != nullptr) {
                if (driver.cuEventCreate(&slot.ready_event, CU_EVENT_DISABLE_TIMING) !=
                    CUDA_SUCCESS) {
                    slot.ready_event = nullptr;
                }
            }
            if (slot.ready_event != nullptr && driver.cuEventRecord != nullptr &&
                driver.cuEventRecord(slot.ready_event, stream) == CUDA_SUCCESS) {
                slot.event_valid = true;
            } else if (driver.cuStreamSynchronize != nullptr) {
                // No event objects on this driver: fall back to a synchronous
                // export so the buffer is safe to hand over.
                result = driver.cuStreamSynchronize(stream);
                slot.event_valid = false;
            }
        }
        functions.unmapVideoFrame(decoder, source);
        if (result != CUDA_SUCCESS) {
            last_error = "cuMemcpy2DAsync failed while exporting a decoded surface";
            free_slots.push_front(slot_index);
            return 0;
        }

        slot.in_use = true;
        RtxFrameDesc frame{};
        frame.planes[0] = slot.pointer;
        frame.planes[1] = slot.pointer + static_cast<CUdeviceptr>(slot.pitch) *
                                           static_cast<CUdeviceptr>(height);
        frame.pitches[0] = static_cast<uint32_t>(slot.pitch);
        frame.pitches[1] = static_cast<uint32_t>(slot.pitch);
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
    if (!self->cuda.open(&error)) {
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    if (!rtx_shim::openNvidiaLibrary(self->cuvid, "nvcuvid.dll", "libnvcuvid.so.1", true,
                                     &error)) {
        delete self;
        return RTX_ERR_DLL_MISSING;
    }
    if (!self->functions.resolve(self->cuvid, &error)) {
        delete self;
        return RTX_ERR_DLL_MISSING;
    }

    // Query the NVDEC capability matrix before building a parser.  Maxwell
    // GM204, for example, has no HEVC decode engine: cuvidCreateDecoder would
    // only fail later, inside the first sequence callback.
    if (self->functions.getDecoderCaps != nullptr) {
        CUVIDDECODECAPS caps{};
        caps.eCodecType = mapCodec(config->codec);
        caps.eChromaFormat = cudaVideoChromaFormat_420;
        caps.nBitDepthMinus8 = static_cast<unsigned int>(self->bit_depth > 8 ? 2 : 0);
        const CUresult caps_result = self->functions.getDecoderCaps(&caps);
        RTX_TRACE("decoder: cuvidGetDecoderCaps codec=%d %d-bit -> %d supported=%d max=%ux%u",
                  config->codec, self->bit_depth, static_cast<int>(caps_result),
                  static_cast<int>(caps.bIsSupported), caps.nMaxWidth, caps.nMaxHeight);
        if (caps_result == CUDA_SUCCESS && caps.bIsSupported == 0) {
            delete self;
            return RTX_ERR_ARCH_UNSUPPORTED;
        }
    }

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
    const CUresult result = self->functions.createParser(&self->parser, &params);
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
        decoder->functions.destroyParser(decoder->parser);
        decoder->parser = nullptr;
    }
    if (decoder->decoder != nullptr) {
        decoder->functions.destroyDecoder(decoder->decoder);
        decoder->decoder = nullptr;
    }
    if (decoder->stream != nullptr) {
        decoder->cuda.driver().cuStreamDestroy(decoder->stream);
        decoder->stream = nullptr;
    }
    for (Slot& slot : decoder->slots) {
        if (slot.ready_event != nullptr && decoder->cuda.driver().cuEventDestroy != nullptr) {
            decoder->cuda.driver().cuEventDestroy(slot.ready_event);
            slot.ready_event = nullptr;
        }
        if (slot.pointer != 0) {
            decoder->cuda.driver().cuMemFree(slot.pointer);
            slot.pointer = 0;
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
    const CUresult result = decoder->functions.parseVideoData(decoder->parser, &packet);
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
    // Opportunistically retire export copies that have already completed.
    if (!decoder->ready.empty()) {
        // The encoder reads the surface on its own engine, so the export copy
        // for this specific slot must have finished before we hand it over.
        decoder->waitSlotReady(decoder->ready.front().slot_index);
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

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_decoder_flush(RtxDecoder* decoder) {
    if (decoder == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (decoder->failed) {
        // A failed sequence/decode callback leaves the CUVID parser in an
        // undefined state; submitting EOS can block indefinitely.
        return RTX_ERR_INTERNAL;
    }
    if (decoder->end_of_stream) {
        return RTX_OK;
    }
    decoder->end_of_stream = true;
    CUVIDSOURCEDATAPACKET packet{};
    // CUVID_PKT_NOTIFY_EOS makes the parser deliver a sentinel display
    // callback; CUVID_PKT_ENDOFSTREAM alone drains the DPB and returns, which
    // is what a synchronous transcode loop needs.
    packet.flags = CUVID_PKT_ENDOFSTREAM;
    packet.payload = nullptr;
    packet.payload_size = 0;
    packet.timestamp = 0;
    RTX_TRACE("decoder: cuvidParseVideoData(end-of-stream) submitting");
    const CUresult result = decoder->functions.parseVideoData(decoder->parser, &packet);
    RTX_TRACE("decoder: cuvidParseVideoData(end-of-stream) -> %d", static_cast<int>(result));
    if (result != CUDA_SUCCESS) {
        return decoder->fail(mapResult(result), "cuvidParseVideoData(end-of-stream) failed");
    }
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL
rtx_shim_decoder_release_frame(RtxDecoder* decoder, uint64_t slot_index) {
    if (decoder == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (slot_index >= decoder->slots.size()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    Slot& slot = decoder->slots[static_cast<size_t>(slot_index)];
    if (!slot.in_use) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    slot.in_use = false;
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
    const Slot& slot = decoder->slots[static_cast<size_t>(frame->slot_index)];
    if (!slot.in_use) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
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

    DriverTable& driver = decoder->cuda.driver();
    rtx_shim::cuda::CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = slot.pointer;
    copy.srcPitch = slot.pitch;
    copy.dstMemoryType = rtx_shim::cuda::CU_MEMORYTYPE_HOST;
    copy.dstHost = destination;
    copy.dstPitch = destination_pitch;
    copy.WidthInBytes = row_bytes;
    copy.Height = static_cast<size_t>(frame->height);
    int result = driver.cuMemcpy2DAsync(&copy, nullptr);
    if (result == rtx_shim::cuda::CUDA_SUCCESS) {
        rtx_shim::cuda::CUDA_MEMCPY2D chroma = copy;
        chroma.srcDevice = slot.pointer + static_cast<CUdeviceptr>(slot.pitch) *
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
