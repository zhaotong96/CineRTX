// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 RTX Transcode Engine contributors
//
// Phase 9 (Task 9-A2): Linux headless pure-CUDA remaster backend.
//
// The expert verdict (§4.5) bans Vulkan external-memory interop on Linux:
// NVDEC hands over CUdeviceptr, NVENC consumes CUdeviceptr, so the only
// Windows-specific stage (NGX VSR + TrueHDR through D3D12) is replaced by
// ONE fused CUDA kernel - directional bicubic + CAS upscale, parameterised
// ITU-R BT.2446 Method C inverse tone mapping, FP16 3D LUT, P010 pack -
// implemented in src/shim/ngx_bridge/p010_pack.cu
// (rtx_cuda_remaster_fused_p010) and shipped as embedded PTX.
//
// Everything here binds the CUDA driver API at run time with
// dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL): no libcudart, no Vulkan, no
// X11.  A container without a GPU still starts; rtx_ngx_bridge_remaster then
// reports RTX_ERR_NGX instead of pretending a device exists.

#include "ngx_bridge_abi.h"

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "p010_pack_runtime.h"

namespace {

// --- Minimal CUDA driver ABI surface (stable since CUDA 4.x) ---------------
using CUresult = int;
using CUdevice = int;
using CUcontext = void*;
using CUstream = void*;
using CUevent = void*;
using CUarray = void*;
using CUtexObject = unsigned long long;
using CUdeviceptr = unsigned long long;
using CUmodule = void*;
using CUfunction = void*;
constexpr CUresult kSuccess = 0;

constexpr int kCuDeviceAttributeComputeCapabilityMajor = 75;
constexpr int kCuDeviceAttributeComputeCapabilityMinor = 76;

constexpr int kCuMemoryTypeHost = 1;
constexpr int kCuMemoryTypeDevice = 2;
constexpr int kCuMemoryTypeArray = 4;

constexpr int kCuAdFormatHalf = 0x10;
constexpr int kCuTrAddressModeClamp = 1;
constexpr int kCuTrFilterModeLinear = 1;
constexpr unsigned kCuTrsfNormalizedCoordinates = 2u;
constexpr int kCuResourceTypeArray = 0;
constexpr unsigned kCuStreamNonBlocking = 1u;

struct CUDA_ARRAY3D_DESCRIPTOR {
    size_t Width;
    size_t Height;
    size_t Depth;
    unsigned int Format;
    unsigned int NumChannels;
    unsigned int Flags;
};

struct CUDA_MEMCPY3D {
    unsigned int srcXInBytes;
    unsigned int srcY;
    unsigned int srcZ;
    unsigned int srcLOD;
    int srcMemoryType;
    const void* srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    unsigned int srcPitch;
    unsigned int dstXInBytes;
    unsigned int dstY;
    unsigned int dstZ;
    unsigned int dstLOD;
    int dstMemoryType;
    void* dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    unsigned int dstPitch;
    unsigned int WidthInBytes;
    unsigned int Height;
    unsigned int Depth;
};

struct CUDA_RESOURCE_DESC {
    int resType;
    union {
        struct {
            CUarray hArray;
        } array;
        char pad[64];
    } res;
};

struct CUDA_TEXTURE_DESC {
    int addressMode[3];
    int filterMode;
    unsigned int flags;
    unsigned int maxAnisotropy;
    int mipmapFilterMode;
    float mipmapLevelBias;
    float minMipmapLevelClamp;
    float maxMipmapLevelClamp;
    float borderColor[4];
    int reserved[12];
};

struct DriverApi {
    CUresult (*init)(unsigned int) = nullptr;
    CUresult (*device_get)(CUdevice*, int) = nullptr;
    CUresult (*device_get_count)(int*) = nullptr;
    CUresult (*device_get_name)(char*, int, CUdevice) = nullptr;
    CUresult (*device_get_attribute)(int*, int, CUdevice) = nullptr;
    CUresult (*device_get_luid)(char*, unsigned int*, CUdevice) = nullptr;
    CUresult (*device_total_mem)(size_t*, CUdevice) = nullptr;
    CUresult (*primary_ctx_retain)(CUcontext*, CUdevice) = nullptr;
    CUresult (*primary_ctx_release)(CUdevice) = nullptr;
    CUresult (*ctx_set_current)(CUcontext) = nullptr;
    CUresult (*stream_create)(CUstream*, unsigned int) = nullptr;
    CUresult (*stream_destroy)(CUstream) = nullptr;
    CUresult (*stream_synchronize)(CUstream) = nullptr;
    CUresult (*mem_alloc_pitch)(CUdeviceptr*, size_t*, size_t, size_t, unsigned int) = nullptr;
    CUresult (*mem_free)(CUdeviceptr) = nullptr;
    CUresult (*memset_d32_async)(CUdeviceptr, unsigned int, size_t, CUstream) = nullptr;
    CUresult (*memcpy_htod)(CUdeviceptr, const void*, size_t) = nullptr;
    CUresult (*memcpy_dtoh)(void*, CUdeviceptr, size_t) = nullptr;
    CUresult (*memcpy3d)(const CUDA_MEMCPY3D*) = nullptr;
    CUresult (*array3d_create)(CUarray*, const CUDA_ARRAY3D_DESCRIPTOR*) = nullptr;
    CUresult (*array_destroy)(CUarray) = nullptr;
    CUresult (*tex_object_create)(CUtexObject*, const CUDA_RESOURCE_DESC*,
                                  const CUDA_TEXTURE_DESC*, const void*) = nullptr;
    CUresult (*tex_object_destroy)(CUtexObject) = nullptr;
    CUresult (*event_create)(CUevent*, unsigned int) = nullptr;
    CUresult (*event_record)(CUevent, CUstream) = nullptr;
    CUresult (*event_elapsed)(float*, CUevent, CUevent) = nullptr;
    CUresult (*event_destroy)(CUevent) = nullptr;
    CUresult (*get_error_name)(CUresult, const char**) = nullptr;

    bool resolve(std::string* error) {
        void* library = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) {
            *error = "libcuda.so.1 is not available";
            return false;
        }
        auto sym = [library](const char* name) {
            return dlsym(library, name);
        };
        auto bind = [&](auto& target, const char* name) {
            target = reinterpret_cast<decltype(target)>(sym(name));
        };
        bind(init, "cuInit");
        bind(device_get, "cuDeviceGet");
        bind(device_get_count, "cuDeviceGetCount");
        bind(device_get_name, "cuDeviceGetName");
        bind(device_get_attribute, "cuDeviceGetAttribute");
        bind(device_get_luid, "cuDeviceGetLuid");
        bind(device_total_mem, "cuDeviceTotalMem_v2");
        bind(primary_ctx_retain, "cuDevicePrimaryCtxRetain");
        bind(primary_ctx_release, "cuDevicePrimaryCtxRelease");
        bind(ctx_set_current, "cuCtxSetCurrent");
        bind(stream_create, "cuStreamCreate");
        bind(stream_destroy, "cuStreamDestroy_v2");
        bind(stream_synchronize, "cuStreamSynchronize_v2");
        bind(mem_alloc_pitch, "cuMemAllocPitch_v2");
        bind(mem_free, "cuMemFree_v2");
        bind(memset_d32_async, "cuMemsetD32Async");
        bind(memcpy_htod, "cuMemcpyHtoD_v2");
        bind(memcpy_dtoh, "cuMemcpyDtoH_v2");
        bind(memcpy3d, "cuMemcpy3D_v2");
        bind(array3d_create, "cuArray3DCreate_v2");
        bind(array_destroy, "cuArrayDestroy");
        bind(tex_object_create, "cuTexObjectCreate");
        bind(tex_object_destroy, "cuTexObjectDestroy");
        bind(event_create, "cuEventCreate");
        bind(event_record, "cuEventRecord_v2");
        bind(event_elapsed, "cuEventElapsedTime");
        bind(event_destroy, "cuEventDestroy_v2");
        bind(get_error_name, "cuGetErrorName");
        if (init == nullptr || device_get == nullptr || primary_ctx_retain == nullptr ||
            ctx_set_current == nullptr || stream_create == nullptr ||
            mem_alloc_pitch == nullptr || mem_free == nullptr || memcpy_htod == nullptr ||
            memcpy_dtoh == nullptr) {
            *error = "the CUDA driver is missing the core entry points";
            return false;
        }
        return true;
    }
};

DriverApi& driver() {
    static DriverApi api;
    return api;
}

const char* errorName(CUresult result) {
    const char* name = nullptr;
    if (driver().get_error_name != nullptr) {
        driver().get_error_name(result, &name);
    }
    return name != nullptr ? name : "CUDA_ERROR";
}

// --- Linux bridge state ----------------------------------------------------
struct LinuxBridge {
    RtxNgxBridgeConfig config{};
    CUcontext context = nullptr;
    CUdevice device = 0;
    CUstream stream = nullptr;
    CUevent start_event = nullptr;
    CUevent stop_event = nullptr;
    // Phase 7 (Task 7-A3): the 3-slot P010 ring.
    CUdeviceptr p010_ring[3] = {0, 0, 0};
    size_t p010_pitch = 0;
    uint64_t remaster_frames_issued = 0;
    CUdeviceptr stats_devptr = 0;
    CUarray lut_array = nullptr;
    CUtexObject lut_tex = 0;
    int lut_dim = 0;
    RtxNgxRemasterStats stats{};
    std::mutex stats_mutex;
    std::string error;
    bool gpu_ready = false;

    ~LinuxBridge() {
        if (driver().ctx_set_current != nullptr && context != nullptr) {
            (void)driver().ctx_set_current(context);
        }
        if (lut_tex != 0 && driver().tex_object_destroy != nullptr) {
            (void)driver().tex_object_destroy(lut_tex);
        }
        if (lut_array != nullptr && driver().array_destroy != nullptr) {
            (void)driver().array_destroy(lut_array);
        }
        for (CUdeviceptr& buffer : p010_ring) {
            if (buffer != 0 && driver().mem_free != nullptr) {
                (void)driver().mem_free(buffer);
                buffer = 0;
            }
        }
        if (stats_devptr != 0 && driver().mem_free != nullptr) {
            (void)driver().mem_free(stats_devptr);
            stats_devptr = 0;
        }
        if (start_event != nullptr && driver().event_destroy != nullptr) {
            (void)driver().event_destroy(start_event);
        }
        if (stop_event != nullptr && driver().event_destroy != nullptr) {
            (void)driver().event_destroy(stop_event);
        }
        if (stream != nullptr && driver().stream_destroy != nullptr) {
            (void)driver().stream_destroy(stream);
        }
        if (context != nullptr && driver().primary_ctx_release != nullptr) {
            (void)driver().primary_ctx_release(device);
        }
    }
};

std::string g_last_linux_bridge_error;

LinuxBridge* asBridge(RtxNgxBridge* bridge) {
    return reinterpret_cast<LinuxBridge*>(bridge);
}

bool validConfig(const RtxNgxBridgeConfig* config) {
    if (config == nullptr) {
        return false;
    }
    return config->input_width > 0 && config->input_height > 0 &&
           config->output_width > 0 && config->output_height > 0 &&
           config->output_width >= config->input_width &&
           config->output_height >= config->input_height &&
           config->vsr_quality >= 0 && config->vsr_quality <= 4 &&
           (config->enable_truehdr == 0 || config->enable_truehdr == 1);
}

bool probeGpu(std::string* error) {
    static bool attempted = false;
    static bool ready = false;
    static int compute_major = 0;
    static int compute_minor = 0;
    if (!attempted) {
        attempted = true;
        DriverApi& api = driver();
        if (!api.resolve(error)) {
            return false;
        }
        if (api.init(0) != kSuccess) {
            *error = "cuInit failed; the NVIDIA kernel driver is not usable";
            return false;
        }
        int count = 0;
        if (api.device_get_count(&count) != kSuccess || count <= 0) {
            *error = "no CUDA device is visible";
            return false;
        }
        CUdevice device = 0;
        if (api.device_get(&device, 0) != kSuccess) {
            *error = "cuDeviceGet failed";
            return false;
        }
        (void)api.device_get_attribute(&compute_major,
                                       kCuDeviceAttributeComputeCapabilityMajor, device);
        (void)api.device_get_attribute(&compute_minor,
                                       kCuDeviceAttributeComputeCapabilityMinor, device);
        ready = compute_major > 7 || (compute_major == 7 && compute_minor >= 5);
        if (!ready) {
            *error = "the CUDA device is older than the sm_75 baseline";
        }
    } else if (!ready && error != nullptr) {
        *error = "the CUDA device is older than the sm_75 baseline";
    }
    return ready;
}

bool allocPitched(LinuxBridge& bridge, CUdeviceptr* pointer, size_t* pitch, size_t width_bytes,
                  size_t rows, std::string* error) {
    const CUresult result =
        driver().mem_alloc_pitch(pointer, pitch, width_bytes, rows, 16);
    if (result != kSuccess) {
        *error = std::string("cuMemAllocPitch failed: ") + errorName(result);
        return false;
    }
    (void)bridge;
    return true;
}

}  // namespace

extern "C" {

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_query_caps(RtxNgxBridgeCaps* caps) {
    if (caps == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::memset(caps, 0, sizeof(*caps));
    std::string error;
    const bool gpu = probeGpu(&error);
    // Phase 9 (Task 9-A2): the pure CUDA backend implements both remaster
    // stages on the tensor cores, so they are always "available" when the
    // sm_75+ GPU is; the D3D12 interop concept does not apply on Linux.
    caps->vsr_available = gpu ? 1 : 0;
    caps->truehdr_available = gpu ? 1 : 0;
    caps->cuda_interop_available = gpu ? 1 : 0;
    caps->d3d12_device_created = 0;
    const std::string diagnostics =
        gpu ? "Linux pure-CUDA remaster backend ready (BT.2446 Method C + CAS upscale)"
            : ("Linux pure-CUDA remaster backend unavailable: " + error);
    std::strncpy(caps->diagnostics, diagnostics.c_str(), sizeof(caps->diagnostics) - 1);
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_create(const RtxNgxBridgeConfig* config,
                                                           RtxNgxBridge** bridge) {
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *bridge = nullptr;
    if (!validConfig(config)) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::string error;
    if (!probeGpu(&error)) {
        g_last_linux_bridge_error = error;
        return RTX_ERR_ARCH_UNSUPPORTED;
    }
    auto impl = std::make_unique<LinuxBridge>();
    impl->config = *config;
    DriverApi& api = driver();
    if (api.device_get(&impl->device, 0) != kSuccess) {
        impl->error = "cuDeviceGet failed";
        return RTX_ERR_ARCH_UNSUPPORTED;
    }
    if (api.primary_ctx_retain(&impl->context, impl->device) != kSuccess ||
        api.ctx_set_current(impl->context) != kSuccess) {
        impl->error = "cannot retain the primary CUDA context";
        return RTX_ERR_ARCH_UNSUPPORTED;
    }
    if (api.stream_create(&impl->stream, kCuStreamNonBlocking) != kSuccess) {
        impl->error = "cuStreamCreate failed";
        return RTX_ERR_ARCH_UNSUPPORTED;
    }
    if (api.event_create(&impl->start_event, 0) != kSuccess ||
        api.event_create(&impl->stop_event, 0) != kSuccess) {
        impl->error = "cuEventCreate failed";
        return RTX_ERR_ARCH_UNSUPPORTED;
    }
    // 3-slot P010 ring (Phase 7 parity) + fused-kernel statistic slots.
    const size_t width_bytes = static_cast<size_t>(config->output_width) * 2u;
    const size_t rows = static_cast<size_t>(config->output_height) * 3u / 2u + 1u;
    for (CUdeviceptr& buffer : impl->p010_ring) {
        if (!allocPitched(*impl, &buffer, &impl->p010_pitch, width_bytes, rows, &error)) {
            impl->error = error;
            return RTX_ERR_OUT_OF_MEMORY;
        }
    }
    {
        size_t stats_pitch = 0;
        if (!allocPitched(*impl, &impl->stats_devptr, &stats_pitch, 16, 1, &error)) {
            impl->error = error;
            return RTX_ERR_OUT_OF_MEMORY;
        }
    }
    impl->gpu_ready = true;
    *bridge = reinterpret_cast<RtxNgxBridge*>(impl.release());
    return RTX_OK;
}

RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy(RtxNgxBridge* bridge) {
    delete asBridge(bridge);
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_remaster(
    RtxNgxBridge* bridge, const RtxNgxRemasterInput* input, RtxNgxRemasterOutput* output,
    RtxNgxRemasterStats* stats) {
    if (bridge == nullptr || input == nullptr || output == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    LinuxBridge* impl = asBridge(bridge);
    if (!impl->gpu_ready) {
        impl->error = "the CUDA device is not ready";
        return RTX_ERR_NGX;
    }
    if (input->y_plane == 0 || input->uv_plane == 0 || input->y_pitch == 0 ||
        input->uv_pitch == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    DriverApi& api = driver();
    (void)api.ctx_set_current(impl->context);

    const uint64_t frame_index = impl->remaster_frames_issued;
    CUdeviceptr p010_buffer = impl->p010_ring[frame_index % 3u];

    // Stream-ordered statistic reset, then the single fused launch
    // (upscale + BT.2446 Method C + 3D LUT + P010 pack).
    (void)api.memset_d32_async(impl->stats_devptr, 0, 4, impl->stream);
    (void)api.event_record(impl->start_event, impl->stream);

    rtx_ngx::FusedRemasterLaunch launch{};
    launch.src_y = input->y_plane;
    launch.src_uv = input->uv_plane;
    launch.src_y_pitch = input->y_pitch;
    launch.src_uv_pitch = input->uv_pitch;
    launch.src_format = input->format == RTX_PIXEL_P010 ? 2 : 1;
    launch.src_full_range = input->full_range;
    launch.src_matrix = input->matrix == 1 ? 1 : (input->matrix == 2 ? 2 : 0);
    launch.dst_y = p010_buffer;
    launch.dst_uv = p010_buffer +
                    static_cast<uint64_t>(impl->p010_pitch) *
                        static_cast<uint64_t>(impl->config.output_height);
    launch.dst_y_pitch_samples = static_cast<uint32_t>(impl->p010_pitch / 2u);
    launch.dst_uv_pitch_samples = launch.dst_y_pitch_samples;
    launch.src_width = impl->config.input_width;
    launch.src_height = impl->config.input_height;
    launch.dst_width = impl->config.output_width;
    launch.dst_height = impl->config.output_height;
    launch.lut_tex = impl->lut_tex;
    launch.lut_dim = impl->lut_dim;
    launch.vsr_quality = std::clamp(impl->config.vsr_quality, 1, 4);
    launch.enable_itm = impl->config.enable_truehdr;
    launch.max_luminance = static_cast<float>(
        impl->config.max_luminance > 0 ? std::clamp(impl->config.max_luminance, 400, 4000) : 1000);
    launch.stats_devptr = impl->stats_devptr;

    const RtxResult launched = rtx_ngx::launchFusedRemaster(launch, impl->stream);
    if (launched != RTX_OK) {
        impl->error = std::string("rtx_cuda_remaster_fused_p010 launch failed: ") +
                      rtx_ngx::packLastError();
        return launched == RTX_ERR_UNSUPPORTED ? RTX_ERR_UNSUPPORTED : RTX_ERR_NGX;
    }
    (void)api.event_record(impl->stop_event, impl->stream);
    if (api.stream_synchronize(impl->stream) != kSuccess) {
        impl->error = "the fused remaster stream failed to synchronise";
        return RTX_ERR_DEVICE_LOST;
    }

    float pack_ms = 0.0f;
    uint32_t stats_host[4] = {0, 0, 0, 0};
    (void)api.event_elapsed(&pack_ms, impl->start_event, impl->stop_event);
    (void)api.memcpy_dtoh(stats_host, impl->stats_devptr, sizeof(stats_host));

    std::memset(output, 0, sizeof(*output));
    output->y_plane = p010_buffer;
    output->uv_plane = launch.dst_uv;
    output->y_pitch = static_cast<uint32_t>(impl->p010_pitch);
    output->uv_pitch = output->y_pitch;
    output->width = impl->config.output_width;
    output->height = impl->config.output_height;
    output->format = RTX_PIXEL_P010;
    output->ring_slot_index = static_cast<uint32_t>(frame_index % 3u);
    // The fused kernel publishes the same physical statistics as the Windows
    // pack kernel; bridge_exports.cpp packs max as an IEEE bit pattern.
    const auto bits_to_float = [](uint32_t bits) {
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };
    const uint32_t samples = static_cast<uint32_t>(impl->config.output_width) *
                             static_cast<uint32_t>(impl->config.output_height);
    const float frame_max = bits_to_float(stats_host[0]);
    const float frame_sum = bits_to_float(stats_host[1]);
    const float frame_avg = samples > 0 ? frame_sum / static_cast<float>(samples) : 0.0f;
    output->frame_max_nits = static_cast<uint32_t>(std::max(0.0f, frame_max) + 0.5f);
    output->frame_avg_nits = static_cast<uint32_t>(std::max(0.0f, frame_avg) + 0.5f);

    impl->stats.preprocess_ms += 0.0;
    impl->stats.ngx_ms += 0.0;
    impl->stats.pack_kernel_ms += pack_ms;
    impl->stats.frame_ms += pack_ms;
    impl->stats.frames += 1;
    impl->stats.pack_launches += 1;
    impl->remaster_frames_issued += 1;
    if (stats != nullptr) {
        *stats = impl->stats;
    }
    impl->error.clear();
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_set_lut3d(
    RtxNgxBridge* bridge, const uint16_t* lut_half4_data, int32_t grid_size) {
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    LinuxBridge* impl = asBridge(bridge);
    if (lut_half4_data == nullptr || grid_size <= 1) {
        if (impl->lut_tex != 0 && driver().tex_object_destroy != nullptr) {
            (void)driver().tex_object_destroy(impl->lut_tex);
        }
        if (impl->lut_array != nullptr && driver().array_destroy != nullptr) {
            (void)driver().array_destroy(impl->lut_array);
        }
        impl->lut_tex = 0;
        impl->lut_array = nullptr;
        impl->lut_dim = 0;
        return RTX_OK;
    }
    if (grid_size < 2 || grid_size > 256) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    DriverApi& api = driver();
    (void)api.ctx_set_current(impl->context);
    CUDA_ARRAY3D_DESCRIPTOR array_desc{};
    array_desc.Width = static_cast<size_t>(grid_size);
    array_desc.Height = static_cast<size_t>(grid_size);
    array_desc.Depth = static_cast<size_t>(grid_size);
    array_desc.Format = kCuAdFormatHalf;
    array_desc.NumChannels = 4;
    array_desc.Flags = 0;
    CUarray array = nullptr;
    if (api.array3d_create == nullptr || api.array3d_create(&array, &array_desc) != kSuccess) {
        impl->error = "cuArray3DCreate failed for the 3D LUT";
        return RTX_ERR_NGX;
    }
    CUDA_MEMCPY3D copy{};
    copy.srcMemoryType = kCuMemoryTypeHost;
    copy.srcHost = lut_half4_data;
    copy.dstMemoryType = kCuMemoryTypeArray;
    copy.dstArray = array;
    copy.WidthInBytes = static_cast<unsigned>(grid_size) * 4u * sizeof(uint16_t);
    copy.Height = static_cast<unsigned>(grid_size);
    copy.Depth = static_cast<unsigned>(grid_size);
    if (api.memcpy3d == nullptr || api.memcpy3d(&copy) != kSuccess) {
        impl->error = "cuMemcpy3D (LUT H2D) failed";
        (void)api.array_destroy(array);
        return RTX_ERR_NGX;
    }
    CUDA_RESOURCE_DESC resource_desc{};
    resource_desc.resType = kCuResourceTypeArray;
    resource_desc.res.array.hArray = array;
    CUDA_TEXTURE_DESC texture_desc{};
    texture_desc.addressMode[0] = kCuTrAddressModeClamp;
    texture_desc.addressMode[1] = kCuTrAddressModeClamp;
    texture_desc.addressMode[2] = kCuTrAddressModeClamp;
    texture_desc.filterMode = kCuTrFilterModeLinear;
    texture_desc.flags = kCuTrsfNormalizedCoordinates;
    CUtexObject tex_object = 0;
    if (api.tex_object_create == nullptr ||
        api.tex_object_create(&tex_object, &resource_desc, &texture_desc, nullptr) != kSuccess) {
        impl->error = "cuTexObjectCreate failed for the 3D LUT";
        (void)api.array_destroy(array);
        return RTX_ERR_NGX;
    }
    // Swap in the new texture and release the previous one.
    CUtexObject old_tex = impl->lut_tex;
    CUarray old_array = impl->lut_array;
    impl->lut_tex = tex_object;
    impl->lut_array = array;
    impl->lut_dim = grid_size;
    if (old_tex != 0) {
        (void)api.tex_object_destroy(old_tex);
    }
    if (old_array != nullptr) {
        (void)api.array_destroy(old_array);
    }
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_preview_buffer(
    RtxNgxBridge* bridge, uint32_t slot, uint64_t* device_ptr, uint32_t* pitch) {
    // The headless Linux backend has no realtime viewport (verdict §4.5:
    // server-side daemon mode only).
    (void)bridge;
    (void)slot;
    if (device_ptr != nullptr) {
        *device_ptr = 0;
    }
    if (pitch != nullptr) {
        *pitch = 0;
    }
    return RTX_ERR_UNSUPPORTED;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_download_preview(
    RtxNgxBridge* bridge, void* destination, size_t destination_bytes) {
    (void)bridge;
    (void)destination;
    (void)destination_bytes;
    return RTX_ERR_UNSUPPORTED;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_remaster_stats(
    RtxNgxBridge* bridge, RtxNgxRemasterStats* stats) {
    if (bridge == nullptr || stats == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(asBridge(bridge)->stats_mutex);
    *stats = asBridge(bridge)->stats;
    return RTX_OK;
}

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_bridge_last_error(RtxNgxBridge* bridge) {
    if (bridge == nullptr) {
        return g_last_linux_bridge_error.c_str();
    }
    return asBridge(bridge)->error.c_str();
}

RTX_SHIM_API uint32_t RTX_SHIM_CALL rtx_ngx_bridge_abi_version(void) {
    return RTX_NGX_BRIDGE_ABI_VERSION;
}

}  // extern "C"
