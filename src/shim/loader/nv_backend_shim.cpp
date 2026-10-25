/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * The NVIDIA shim.  It is the only binary that resolves NVIDIA entry points,
 * and it does so exclusively through LoadLibraryEx/GetProcAddress.  The GPL
 * core sees opaque handles and plain C structs from rtx_shim_abi.h.
 */

#define RTX_SHIM_EXPORTS 1

#include "rtx_shim_abi.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

#include "dyn_library.h"
#include "nv_func_tables.h"

namespace {

struct ProbeState {
    rtx_shim::NvidiaRuntime runtime;
    rtx_shim::cuda::DriverTable driver;
    RtxDeviceInfo device{};
    uint32_t capabilities = 0;
    bool cuda_loaded = false;
    bool cuda_initialized = false;
    bool has_device = false;
    bool interop_ready = false;
    std::string summary;
};

template <size_t N>
void copyText(char (&destination)[N], const std::string& source) {
    size_t count = std::min(source.size(), N - 1);
    // Never split a multi-byte UTF-8 sequence.  Windows error strings are
    // localized, and truncating mid-codepoint used to emit invalid JSON.
    while (count > 0 && count < source.size() &&
           (static_cast<unsigned char>(source[count]) & 0xC0) == 0x80) {
        --count;
    }
    if (count > 0) {
        std::memcpy(destination, source.data(), count);
    }
    destination[count] = '\0';
}

std::string cudaVersionString(int version) {
    if (version <= 0) {
        return "unknown";
    }
    return std::to_string(version / 1000) + "." + std::to_string((version / 10) % 100);
}

const rtx_shim::ComponentStatus* findComponent(const ProbeState& state, const char* name) {
    for (const auto& component : state.runtime.components()) {
        if (component.name == name) {
            return &component;
        }
    }
    return nullptr;
}

bool componentPresent(const ProbeState& state, const char* name) {
    const auto* component = findComponent(state, name);
    return component != nullptr && component->present;
}

void fillComponent(RtxComponentInfo& out, const rtx_shim::ComponentStatus& component) {
    copyText(out.name, component.name);
    copyText(out.path, component.path);
    out.present = component.present ? 1 : 0;
    out.version = static_cast<int32_t>(component.version);
    copyText(out.note, component.note);
}

void fillDiagnostics(const ProbeState& state, RtxDiagnostics* diagnostics) {
    if (diagnostics == nullptr) {
        return;
    }
    std::memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->device = state.device;
    const uint32_t count = static_cast<uint32_t>(std::min<size_t>(
        state.runtime.components().size(), RTX_MAX_COMPONENTS));
    diagnostics->component_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        fillComponent(diagnostics->components[i], state.runtime.components()[i]);
    }
    copyText(diagnostics->summary, state.summary);
}

ProbeState probeNvidia() {
    ProbeState state;
    std::string ignored;
    state.cuda_loaded = state.runtime.loadCuda(&ignored);
    state.runtime.loadNvenc(&ignored);
    state.runtime.loadNvcuvid(&ignored);
    state.runtime.loadNvOfruc(&ignored);
    state.runtime.loadTensorRt(&ignored);
    state.runtime.loadNgx(&ignored);

    int cuda_version = 0;
    int device_count = 0;
    std::string detail;
    if (state.cuda_loaded) {
        if (!state.driver.resolve(state.runtime.cuda(), &detail)) {
            state.summary = "nvcuda.dll loaded but the driver API is incomplete: " + detail;
        } else if (state.driver.cuInit(0) != rtx_shim::cuda::CUDA_SUCCESS) {
            state.summary = "cuInit failed; the NVIDIA kernel driver is not usable";
        } else {
            state.cuda_initialized = true;
            state.driver.cuDriverGetVersion(&cuda_version);
            state.device.cuda_version = static_cast<uint32_t>(cuda_version);
            if (state.driver.cuDeviceGetCount(&device_count) != rtx_shim::cuda::CUDA_SUCCESS ||
                device_count <= 0) {
                state.summary = "no CUDA devices are visible";
            } else {
                rtx_shim::cuda::CUdevice device = 0;
                state.driver.cuDeviceGet(&device, 0);
                char name[RTX_MAX_DEVICE_NAME] = {};
                state.driver.cuDeviceGetName(name, RTX_MAX_DEVICE_NAME, device);
                copyText(state.device.name, std::string(name));

                size_t total_memory = 0;
                state.driver.cuDeviceTotalMem(&total_memory, device);
                state.device.vram_bytes = total_memory;

                int major = 0;
                int minor = 0;
                state.driver.cuDeviceGetAttribute(
                    &major, rtx_shim::cuda::CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device);
                state.driver.cuDeviceGetAttribute(
                    &minor, rtx_shim::cuda::CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device);
                state.device.compute_major = major;
                state.device.compute_minor = minor;

                char luid[8] = {};
                unsigned int node_mask = 0;
                if (state.driver.cuDeviceGetLuid(luid, &node_mask, device) ==
                    rtx_shim::cuda::CUDA_SUCCESS) {
                    std::memcpy(state.device.luid, luid, 8);
                    state.device.luid_node_mask = node_mask;
                }
                state.device.capabilities = 0;
                state.has_device = true;
                state.interop_ready = state.driver.readyForInterop();

                const bool turing_or_newer =
                    major > 7 || (major == 7 && minor >= 5);
                const bool nvenc_present = componentPresent(state, "NVENC");
                const bool nvdec_present = componentPresent(state, "NVDEC (cuvid)");
                const bool ngx_present = componentPresent(state, "RTX Video SDK (NGX)");
                const bool nvofruc_present = componentPresent(state, "NvOFFRUC");
                const bool tensorrt_present = componentPresent(state, "TensorRT") &&
                                              state.runtime.hasTensorRt();
                uint32_t nvenc_version = state.runtime.nvencVersion();

                if (nvdec_present) {
                    state.capabilities |= RTX_CAP_NVDEC;
                }
                if (nvenc_present && (nvenc_version == 0 || nvenc_version >= 1000)) {
                    state.capabilities |= RTX_CAP_NVENC_HEVC10;
                }
                if (nvenc_present && nvenc_version >= 1200 && (major > 8 || (major == 8 && minor >= 9))) {
                    state.capabilities |= RTX_CAP_NVENC_AV1;
                }
                if (turing_or_newer && ngx_present) {
                    state.capabilities |= RTX_CAP_VSR | RTX_CAP_TRUEHDR;
                }
                if (turing_or_newer && nvofruc_present) {
                    state.capabilities |= RTX_CAP_NVOFA;
                }
                if (tensorrt_present) {
                    state.capabilities |= RTX_CAP_TENSORRT;
                }
#if defined(_WIN32)
                if (state.interop_ready) {
                    state.capabilities |= RTX_CAP_D3D12_INTEROP;
                }
#endif
                state.device.capabilities = state.capabilities;

                std::string summary = "NVIDIA " + std::string(name);
                summary += " sm_" + std::to_string(major) + std::to_string(minor);
                summary += ", CUDA " + cudaVersionString(cuda_version);
                summary += ", " + std::to_string(total_memory / (1024ull * 1024ull)) + " MiB";
                summary += turing_or_newer ? "; RTX Video baseline met"
                                           : "; below Turing 7.5 baseline (RTX Video disabled)";
                if (!state.summary.empty()) {
                    summary += "; " + state.summary;
                }
                state.summary = summary;
            }
        }
    }

    if (state.summary.empty()) {
        std::string reason;
        const auto* cuda = findComponent(state, "CUDA driver");
        if (cuda != nullptr && !cuda->note.empty()) {
            reason = cuda->note;
        }
        state.summary = "NVIDIA CUDA driver unavailable";
        if (!reason.empty()) {
            state.summary += ": " + reason;
        }
        state.summary += "; CPU reference backend remains available";
    }
    return state;
}

}  // namespace

struct RtxBackend {
    RtxBackendConfig config{};
    RtxDeviceInfo device{};
    uint32_t capabilities = 0;
    std::string error;
};

extern "C" {

RTX_SHIM_API uint32_t RTX_SHIM_CALL rtx_shim_abi_version(void) { return RTX_SHIM_ABI_VERSION; }

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_result_name(RtxResult result) {
    switch (result) {
        case RTX_OK:
            return "RTX_OK";
        case RTX_ERR_INVALID_ARGUMENT:
            return "RTX_ERR_INVALID_ARGUMENT";
        case RTX_ERR_NOT_FOUND:
            return "RTX_ERR_NOT_FOUND";
        case RTX_ERR_IO:
            return "RTX_ERR_IO";
        case RTX_ERR_UNSUPPORTED:
            return "RTX_ERR_UNSUPPORTED";
        case RTX_ERR_DLL_MISSING:
            return "RTX_ERR_DLL_MISSING";
        case RTX_ERR_ARCH_UNSUPPORTED:
            return "RTX_ERR_ARCH_UNSUPPORTED";
        case RTX_ERR_DRIVER_TOO_OLD:
            return "RTX_ERR_DRIVER_TOO_OLD";
        case RTX_ERR_DEVICE_LOST:
            return "RTX_ERR_DEVICE_LOST";
        case RTX_ERR_OUT_OF_MEMORY:
            return "RTX_ERR_OUT_OF_MEMORY";
        case RTX_ERR_NGX:
            return "RTX_ERR_NGX";
        case RTX_ERR_TENSORRT_VERSION:
            return "RTX_ERR_TENSORRT_VERSION";
        case RTX_ERR_NOT_IMPLEMENTED:
            return "RTX_ERR_NOT_IMPLEMENTED";
        case RTX_ERR_INTERNAL:
            return "RTX_ERR_INTERNAL";
    }
    return "RTX_ERR_UNKNOWN";
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_probe(RtxDiagnostics* diagnostics) {
    const ProbeState state = probeNvidia();
    fillDiagnostics(state, diagnostics);
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_create_backend(const RtxBackendConfig* config,
                                                             RtxBackend** backend) {
    if (backend == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *backend = nullptr;
    if (config == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (config->width <= 0 || config->height <= 0 || config->target_width <= 0 ||
        config->target_height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (config->vsr_quality < 1 || config->vsr_quality > 4) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (config->target_width < config->width || config->target_height < config->height) {
        return RTX_ERR_UNSUPPORTED;
    }

    const ProbeState state = probeNvidia();
    if (!state.cuda_loaded || !state.has_device) {
        return RTX_ERR_DLL_MISSING;
    }

    const bool turing_or_newer =
        state.device.compute_major > 7 ||
        (state.device.compute_major == 7 && state.device.compute_minor >= 5);
    const bool upscale = config->target_width != config->width ||
                         config->target_height != config->height;

    // NVDEC and NVENC are the only stage-1 requirements.  They exist on
    // Maxwell (sm_52), so the hardware backend is created there as well; only
    // the RTX Video features below are gated on Turing.
    if ((state.capabilities & RTX_CAP_NVDEC) == 0 ||
        !componentPresent(state, "NVENC")) {
        return RTX_ERR_ARCH_UNSUPPORTED;
    }

    if (upscale) {
        if (!turing_or_newer) {
            return RTX_ERR_ARCH_UNSUPPORTED;
        }
        if ((state.capabilities & RTX_CAP_VSR) == 0) {
            return RTX_ERR_NGX;
        }
    }
    if (config->enable_truehdr != 0) {
        if (!turing_or_newer) {
            return RTX_ERR_ARCH_UNSUPPORTED;
        }
        if ((state.capabilities & RTX_CAP_TRUEHDR) == 0) {
            return RTX_ERR_NGX;
        }
    }
    if (config->enable_nvofa != 0) {
        if (!turing_or_newer) {
            return RTX_ERR_ARCH_UNSUPPORTED;
        }
        if ((state.capabilities & RTX_CAP_NVOFA) == 0) {
            return RTX_ERR_UNSUPPORTED;
        }
    }
    if (config->enable_rife != 0) {
        if ((state.capabilities & RTX_CAP_TENSORRT) == 0) {
            return RTX_ERR_TENSORRT_VERSION;
        }
        // RIFE is a stage-3 feature; the stage-1 skeleton intentionally does
        // not pretend to execute TensorRT inference.
        return RTX_ERR_NOT_IMPLEMENTED;
    }

    auto* created = new (std::nothrow) RtxBackend();
    if (created == nullptr) {
        return RTX_ERR_OUT_OF_MEMORY;
    }
    created->config = *config;
    created->device = state.device;
    created->capabilities = state.capabilities;
    created->error = state.summary;
    *backend = created;
    return RTX_OK;
}

RTX_SHIM_API void RTX_SHIM_CALL rtx_shim_destroy_backend(RtxBackend* backend) { delete backend; }

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_get_device_info(RtxBackend* backend,
                                                              RtxDeviceInfo* info) {
    if (backend == nullptr || info == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *info = backend->device;
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_get_capabilities(RtxBackend* backend,
                                                               uint32_t* capabilities) {
    if (backend == nullptr || capabilities == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *capabilities = backend->capabilities;
    return RTX_OK;
}

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_shim_last_error(RtxBackend* backend) {
    if (backend == nullptr) {
        return "invalid RtxBackend handle";
    }
    return backend->error.c_str();
}

}  // extern "C"
