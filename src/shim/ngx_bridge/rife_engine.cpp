/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT 10.13.3.9 Practical-RIFE v4.25 inference engine.
 */

#include "rife_engine.h"
#include "trt_loader.h"
#include "rife_pack_runtime.h"
#include "ngx_bridge_abi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "NvInfer.h"

namespace rtx_ngx {

namespace {

class RifeTrtLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            std::fprintf(stderr, "[RIFE-TRT] %s\n", msg);
        }
    }
};

using CUresult = int;
using CUdeviceptr = unsigned long long;
using CUstream = void*;

struct CudaDriverApi {
    CUresult (*mem_alloc)(CUdeviceptr*, size_t) = nullptr;
    CUresult (*mem_free)(CUdeviceptr) = nullptr;
    CUresult (*mem_alloc_host)(void**, size_t) = nullptr;
    CUresult (*mem_free_host)(void*) = nullptr;
    CUresult (*memcpy_htod)(CUdeviceptr, const void*, size_t) = nullptr;
    CUresult (*memcpy_dtoh)(void*, CUdeviceptr, size_t) = nullptr;
    CUresult (*memcpy_dtod_async)(CUdeviceptr, CUdeviceptr, size_t, CUstream) = nullptr;
    CUresult (*memset_d32_async)(CUdeviceptr, unsigned int, size_t, CUstream) = nullptr;
    CUresult (*stream_sync)(CUstream) = nullptr;
    bool loaded = false;

    bool init() {
        if (loaded) return true;
#if defined(_WIN32)
        HMODULE h = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h) return false;
        mem_alloc = reinterpret_cast<decltype(mem_alloc)>(GetProcAddress(h, "cuMemAlloc_v2"));
        if (!mem_alloc) mem_alloc = reinterpret_cast<decltype(mem_alloc)>(GetProcAddress(h, "cuMemAlloc"));
        mem_free = reinterpret_cast<decltype(mem_free)>(GetProcAddress(h, "cuMemFree_v2"));
        if (!mem_free) mem_free = reinterpret_cast<decltype(mem_free)>(GetProcAddress(h, "cuMemFree"));
        mem_alloc_host = reinterpret_cast<decltype(mem_alloc_host)>(GetProcAddress(h, "cuMemAllocHost_v2"));
        if (!mem_alloc_host) mem_alloc_host = reinterpret_cast<decltype(mem_alloc_host)>(GetProcAddress(h, "cuMemAllocHost"));
        mem_free_host = reinterpret_cast<decltype(mem_free_host)>(GetProcAddress(h, "cuMemFreeHost_v2"));
        if (!mem_free_host) mem_free_host = reinterpret_cast<decltype(mem_free_host)>(GetProcAddress(h, "cuMemFreeHost"));
        memcpy_htod = reinterpret_cast<decltype(memcpy_htod)>(GetProcAddress(h, "cuMemcpyHtoD_v2"));
        if (!memcpy_htod) memcpy_htod = reinterpret_cast<decltype(memcpy_htod)>(GetProcAddress(h, "cuMemcpyHtoD"));
        memcpy_dtoh = reinterpret_cast<decltype(memcpy_dtoh)>(GetProcAddress(h, "cuMemcpyDtoH_v2"));
        if (!memcpy_dtoh) memcpy_dtoh = reinterpret_cast<decltype(memcpy_dtoh)>(GetProcAddress(h, "cuMemcpyDtoH"));
        memcpy_dtod_async = reinterpret_cast<decltype(memcpy_dtod_async)>(GetProcAddress(h, "cuMemcpyDtoDAsync_v2"));
        if (!memcpy_dtod_async) memcpy_dtod_async = reinterpret_cast<decltype(memcpy_dtod_async)>(GetProcAddress(h, "cuMemcpyDtoDAsync"));
        memset_d32_async = reinterpret_cast<decltype(memset_d32_async)>(GetProcAddress(h, "cuMemsetD32Async"));
        stream_sync = reinterpret_cast<decltype(stream_sync)>(GetProcAddress(h, "cuStreamSynchronize"));
        loaded = (mem_alloc && mem_free && memcpy_dtoh && memcpy_dtod_async && stream_sync);
        return loaded;
#else
        return false;
#endif
    }
};

static CudaDriverApi g_cuda;

/* MSVC-safe environment read (Phase 9): the project builds with /WX and MSVC
 * emits C4996 for getenv; suppress exactly that warning around the call. */
const char* readEnvValue(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
    return std::getenv(name);
#pragma warning(pop)
#else
    return std::getenv(name);
#endif
}

/* Anti-pollution guard (RC-3): prebuilt Practical-RIFE engines are multi-MB.
 * Any candidate engine file below 512 KiB is a legacy "__builtin__" synthetic
 * placeholder that must never be treated as a real RIFE model, never loaded,
 * and never written over the official rife_v4.25.engine slot again. */
bool isDirtyEnginePlaceholder(const std::filesystem::path& candidate) {
    std::error_code probe_ec;
    if (!std::filesystem::exists(candidate, probe_ec)) return false;
    const auto fsize = std::filesystem::file_size(candidate, probe_ec);
    if (probe_ec) return false;
    // Legitimate synthesized test engines are intentionally small (< 512 KiB)
    // and must never be treated as dirty placeholders when running under
    // the CINERTX_ALLOW_BUILTIN_RIFE_SYNTH test unlock.
    if (candidate.filename() == "rife_builtin_synth.engine") {
        return false;
    }
    if (fsize >= 512u * 1024u) return false;
    std::fprintf(stderr,
                 "[RIFE-ENGINE] Ignoring dirty placeholder engine %s (%llu bytes < 512 KiB): "
                 "legacy builtin synth artifact, refusing to load\n",
                 candidate.string().c_str(),
                 static_cast<unsigned long long>(fsize));
    return true;
}

}  // namespace

namespace {

/* Human-readable dtype name for loud, actionable load failures. */
std::string dtypeToString(nvinfer1::DataType dtype) {
    switch (dtype) {
        case nvinfer1::DataType::kFLOAT: return "kFLOAT (FP32)";
        case nvinfer1::DataType::kHALF: return "kHALF (FP16)";
        case nvinfer1::DataType::kINT8: return "kINT8";
        case nvinfer1::DataType::kINT32: return "kINT32";
        case nvinfer1::DataType::kBOOL: return "kBOOL";
        default: return "unknown(" + std::to_string(static_cast<int>(dtype)) + ")";
    }
}

}  // namespace

struct RifeEngine::Impl {
    RifeTrtLogger logger;
    nvinfer1::IRuntime* runtime = nullptr;
    nvinfer1::ICudaEngine* engine = nullptr;
    nvinfer1::IExecutionContext* context = nullptr;

    std::string in_name;
    std::string out_name;
    std::string img0_name;
    std::string img1_name;
    std::string timestep_name;

    /* Engine I/O element types (Phase 9.1 RW-P9-02): the preprocess always
     * writes IEEE-754 binary32 into d_input_fp32_scratch and the postprocess
     * always reads IEEE-754 binary32 from d_output_fp32_scratch; kHALF engine
     * I/O adds an explicit conversion chain around enqueueV3. */
    nvinfer1::DataType in_dtype = nvinfer1::DataType::kFLOAT;
    nvinfer1::DataType out_dtype = nvinfer1::DataType::kFLOAT;

    uint64_t d_input_tensor = 0;
    uint64_t d_output_tensor = 0;
    uint64_t d_input_fp32_scratch = 0;
    uint64_t d_output_fp32_scratch = 0;
    uint64_t d_img0_tensor = 0;
    uint64_t d_img1_tensor = 0;
    uint64_t d_timestep_tensor = 0;
    uint64_t d_sad_sum = 0;

    size_t input_bytes = 0;
    size_t output_bytes = 0;

    /* Phase 9 frame-pair cache state (SingleTensor7Ch only). The scheduler
     * holds the prev slot lease across the FRUC window, so adjacent windows
     * always carry a different prev pointer: plain pointer identity can never
     * false-hit across window boundaries. No content hashing by design. */
    uint64_t last_prev_nv12 = 0;
    uint64_t last_curr_nv12 = 0;
    uint32_t last_pitch = 0;
    int32_t last_scene_cut = 0;
    bool cache_valid = false;

    bool alpha_probe_passed = false;

    /* Phase 9 statistics ([RIFE-STATS] at teardown). Invariant:
     * interpolate_calls == preprocess_launches + cache_reuse. */
    uint32_t preprocess_launches = 0;
    uint32_t interpolate_calls = 0;
    uint32_t cache_reuse = 0;

    ~Impl() {
        releaseBuffers();
        std::fprintf(stderr, "[RIFE-STATS] preprocess=%u interpolate=%u cache_reuse=%u\n",
                     preprocess_launches, interpolate_calls, cache_reuse);
        if (context) {
            delete context;
            context = nullptr;
        }
        if (engine) {
            delete engine;
            engine = nullptr;
        }
        if (runtime) {
            delete runtime;
            runtime = nullptr;
        }
    }

    void resetFramePairCacheState() {
        last_prev_nv12 = 0;
        last_curr_nv12 = 0;
        last_pitch = 0;
        last_scene_cut = 0;
        cache_valid = false;
    }

    void releaseBuffers() {
        if (!g_cuda.init()) return;
        /* d_input_tensor / d_output_tensor alias the fp32 scratch for kFLOAT
         * engines; only free them when they own a distinct allocation. */
        if (d_input_tensor && d_input_tensor != d_input_fp32_scratch) {
            g_cuda.mem_free(d_input_tensor);
            d_input_tensor = 0;
        }
        if (d_output_tensor && d_output_tensor != d_output_fp32_scratch) {
            g_cuda.mem_free(d_output_tensor);
            d_output_tensor = 0;
        }
        if (d_input_fp32_scratch) { g_cuda.mem_free(d_input_fp32_scratch); d_input_fp32_scratch = 0; }
        if (d_output_fp32_scratch) { g_cuda.mem_free(d_output_fp32_scratch); d_output_fp32_scratch = 0; }
        if (d_input_tensor) { g_cuda.mem_free(d_input_tensor); d_input_tensor = 0; }
        if (d_output_tensor) { g_cuda.mem_free(d_output_tensor); d_output_tensor = 0; }
        if (d_img0_tensor) { g_cuda.mem_free(d_img0_tensor); d_img0_tensor = 0; }
        if (d_img1_tensor) { g_cuda.mem_free(d_img1_tensor); d_img1_tensor = 0; }
        if (d_timestep_tensor) { g_cuda.mem_free(d_timestep_tensor); d_timestep_tensor = 0; }
        if (d_sad_sum) { g_cuda.mem_free(d_sad_sum); d_sad_sum = 0; }
        input_bytes = 0;
        output_bytes = 0;
        resetFramePairCacheState();
    }
};

RifeEngine::RifeEngine() : impl_(std::make_unique<Impl>()) {}

RifeEngine::~RifeEngine() = default;

bool RifeEngine::isLoaded() const noexcept {
    return impl_ && impl_->context != nullptr;
}

bool RifeEngine::loadEngine(const char* engine_path, uint32_t width, uint32_t height, std::string* error) {
    if (!engine_path || width == 0 || height == 0) {
        if (error) *error = "Invalid engine path or dimensions";
        return false;
    }

    if (!g_cuda.init()) {
        if (error) *error = "Failed to initialize CUDA driver APIs for RIFE";
        return false;
    }

    if (!TrtLoader::instance().loadRuntime(error)) {
        return false;
    }

    // Phase 9 (RC-1): fail loudly at load time when the RIFE PTX pre/post
    // processing module cannot JIT-load, instead of silently degrading to
    // 1:5 duplicate frames from the first interpolate() call.
    if (!isRifePackAvailable()) {
        if (error) {
            *error = "RIFE PTX pre/post-processing module failed to JIT compile: " +
                     std::string(rifePackLastError());
        }
        return false;
    }

    std::filesystem::path resolved_engine(engine_path);
    std::error_code ec;

    // Dirty-placeholder check applies to the explicit engine path as well.
    if (isDirtyEnginePlaceholder(resolved_engine)) {
        resolved_engine.clear();
    }
    ec.clear();

    bool found_existing = !resolved_engine.empty() &&
                          std::filesystem::exists(resolved_engine, ec) &&
                          std::filesystem::file_size(resolved_engine, ec) > 0;
    if (!found_existing) {
        std::vector<std::filesystem::path> base_dirs;
        std::filesystem::path primary_exe_dir;
        std::filesystem::path shared_ext_models_dir;
#if defined(_WIN32)
        wchar_t mod_buf[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, mod_buf, MAX_PATH) > 0) {
            primary_exe_dir = std::filesystem::path(mod_buf).parent_path();
            base_dirs.push_back(primary_exe_dir);
            base_dirs.push_back(primary_exe_dir / "models");
            base_dirs.push_back(primary_exe_dir / "plugins" / "nvidia_rtx");
            std::filesystem::path anc = primary_exe_dir;
            for (int depth = 0; depth < 6 && !anc.empty() && anc != anc.root_path(); ++depth) {
                anc = anc.parent_path();
                if (anc.empty()) break;
                base_dirs.push_back(anc / "models");
                base_dirs.push_back(anc / "external" / "models");
                base_dirs.push_back(anc / ".vs" / "build" / "external" / "models");
                base_dirs.push_back(anc);
                if (shared_ext_models_dir.empty() &&
                    std::filesystem::exists(anc / "external" / "models", ec)) {
                    shared_ext_models_dir = anc / "external" / "models";
                }
            }
        }
#endif
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!cwd.empty()) {
            base_dirs.push_back(cwd);
            base_dirs.push_back(cwd / "models");
            base_dirs.push_back(cwd / ".vs" / "build" / "external" / "models");
            if (shared_ext_models_dir.empty() &&
                std::filesystem::exists(cwd / ".vs" / "build" / "external" / "models", ec)) {
                shared_ext_models_dir = cwd / ".vs" / "build" / "external" / "models";
            }
        }

        bool found_candidate = false;
        for (const auto& dir : base_dirs) {
            for (const char* name : {"rife_v4.25.engine", "rife.engine"}) {
                std::filesystem::path candidate = dir / name;
                if (std::filesystem::exists(candidate, ec) &&
                    std::filesystem::file_size(candidate, ec) > 0 &&
                    !isDirtyEnginePlaceholder(candidate)) {
                    resolved_engine = candidate;
                    found_candidate = true;
                    break;
                }
            }
            if (found_candidate) break;
        }
        found_existing = found_candidate;

        if (!found_existing) {
            // Locate an ONNX source if present, otherwise synthesize via built-in TensorRT network
            std::string onnx_source = "__builtin__";
            for (const auto& dir : base_dirs) {
                for (const char* onnx_name : {"rife_v4.25.onnx", "rife_v4.25_v2.onnx",
                                              "rife_v4.25_heavy.onnx", "rife.onnx",
                                              "model.onnx", "rife_v2/rife_v4.25.onnx"}) {
                    std::filesystem::path candidate = dir / onnx_name;
                    if (std::filesystem::exists(candidate, ec) && std::filesystem::file_size(candidate, ec) > 256u) {
                        onnx_source = candidate.string();
                        break;
                    }
                }
                if (onnx_source != "__builtin__") break;
            }

            if (onnx_source == "__builtin__") {
                // Guard rail (RC-3): the builtin synthetic network is only
                // allowed with an explicit test-environment unlock, and its
                // engine is quarantined under rife_builtin_synth.engine. It
                // must never write or overwrite the official
                // models/rife_v4.25.engine slot.
                const char* allow_synth = readEnvValue("CINERTX_ALLOW_BUILTIN_RIFE_SYNTH");
                const bool builtin_allowed =
                    (allow_synth != nullptr && std::strcmp(allow_synth, "1") == 0);
                if (!builtin_allowed) {
                    if (error) {
                        *error =
                            "No valid RIFE engine (.engine) or ONNX model found; the builtin "
                            "synthetic network is disabled by default to prevent polluting "
                            "models/rife_v4.25.engine. Set environment variable "
                            "CINERTX_ALLOW_BUILTIN_RIFE_SYNTH=1 to allow the synthesized test "
                            "engine (models/rife_builtin_synth.engine).";
                    }
                    return false;
                }
                const std::filesystem::path synth_output =
                    base_dirs.empty()
                        ? std::filesystem::path("models/rife_builtin_synth.engine")
                        : (base_dirs.front() / "models" / "rife_builtin_synth.engine");
                resolved_engine = synth_output;
            } else if (resolved_engine.empty() || resolved_engine.is_relative()) {
                // Real ONNX-derived engines are legitimate and may occupy the
                // official engine filename beside the executable.
                const std::filesystem::path out_base =
                    !primary_exe_dir.empty() ? primary_exe_dir
                                             : (!base_dirs.empty() ? base_dirs.front() : std::filesystem::path("."));
                resolved_engine = out_base / "models" / "rife_v4.25.engine";
            }
            char build_err[512] = {};
            const std::string out_engine_str = resolved_engine.string();
            const int32_t build_res = rtx_ngx_bridge_build_rife_engine(
                onnx_source.c_str(), out_engine_str.c_str(), width, height, build_err, sizeof(build_err));
            if (build_res != RTX_OK) {
                if (error) *error = std::string("Auto-build RIFE engine failed: ") + build_err;
                return false;
            }
            if (onnx_source != "__builtin__" && !shared_ext_models_dir.empty()) {
                std::filesystem::create_directories(shared_ext_models_dir, ec);
                std::filesystem::copy_file(
                    resolved_engine,
                    shared_ext_models_dir / "rife_v4.25.engine",
                    std::filesystem::copy_options::overwrite_existing,
                    ec);
            }
        }
    }

    std::ifstream file(resolved_engine, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        if (error) *error = std::string("Cannot open RIFE engine file: ") + resolved_engine.string();
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        if (error) *error = "RIFE engine file is empty";
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size)) {
        if (error) *error = "Failed to read RIFE engine file into memory";
        return false;
    }

    impl_->runtime = static_cast<nvinfer1::IRuntime*>(
        TrtLoader::instance().createInferRuntime(&impl_->logger));
    if (!impl_->runtime) {
        if (error) *error = "createInferRuntime failed";
        return false;
    }

    builtin_synth_ = (buffer.size() < 2u * 1024u * 1024u);
    impl_->engine = impl_->runtime->deserializeCudaEngine(buffer.data(), buffer.size());
    if (!impl_->engine) {
        if (error) *error = "Failed to deserialize CUDA engine (SM architecture mismatch?)";
        return false;
    }

    impl_->context = impl_->engine->createExecutionContext();
    if (!impl_->context) {
        if (error) *error = "Failed to create execution context for RIFE engine";
        return false;
    }

    width_ = width;
    height_ = height;

    // Discover I/O tensors via modern TensorRT 10.x Tensor Name API
    const int32_t nbTensors = impl_->engine->getNbIOTensors();
    int inCount = 0;
    for (int32_t i = 0; i < nbTensors; ++i) {
        const char* name = impl_->engine->getIOTensorName(i);
        const auto mode = impl_->engine->getTensorIOMode(name);
        if (mode == nvinfer1::TensorIOMode::kINPUT) {
            inCount++;
            if (impl_->in_name.empty()) impl_->in_name = name;
            std::string s(name);
            for (auto& c : s) c = static_cast<char>(tolower(c));
            if (s.find("0") != std::string::npos || s.find("prev") != std::string::npos) {
                impl_->img0_name = name;
            } else if (s.find("1") != std::string::npos || s.find("curr") != std::string::npos) {
                impl_->img1_name = name;
            } else if (s.find("time") != std::string::npos || s.find("step") != std::string::npos || s.find("alpha") != std::string::npos) {
                impl_->timestep_name = name;
            }
        } else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
            impl_->out_name = name;
        }
    }

    if (inCount >= 3) {
        variant_ = Variant::MultiTensor3Input;
        if (impl_->img0_name.empty()) impl_->img0_name = impl_->engine->getIOTensorName(0);
        if (impl_->img1_name.empty()) impl_->img1_name = impl_->engine->getIOTensorName(1);
        if (impl_->timestep_name.empty()) impl_->timestep_name = impl_->engine->getIOTensorName(2);
    } else {
        variant_ = Variant::SingleTensor7Ch;
    }

    const char* check_tensor_name = (variant_ == Variant::SingleTensor7Ch) ? impl_->in_name.c_str() : impl_->img0_name.c_str();
    const auto inType = impl_->engine->getTensorDataType(check_tensor_name);
    if (inType != nvinfer1::DataType::kFLOAT && inType != nvinfer1::DataType::kHALF) {
        if (error) {
            *error = "RIFE model input tensor dtype " + dtypeToString(inType) +
                     " unsupported: expected kFLOAT (FP32) or kHALF (FP16)";
        }
        return false;
    }
    impl_->in_dtype = inType;
    if (variant_ == Variant::SingleTensor7Ch) {
        const auto outType = impl_->engine->getTensorDataType(impl_->out_name.c_str());
        if (outType != nvinfer1::DataType::kFLOAT && outType != nvinfer1::DataType::kHALF) {
            if (error) {
                *error = "RIFE model output tensor dtype " + dtypeToString(outType) +
                         " unsupported: expected kFLOAT (FP32) or kHALF (FP16)";
            }
            return false;
        }
        impl_->out_dtype = outType;
    } else {
        // The MultiTensor3Input variant loud-fails at interpolate time; only
        // the fp32 buffer layout exists for it.
        impl_->in_dtype = inType;
        impl_->out_dtype = nvinfer1::DataType::kFLOAT;
        if (inType != nvinfer1::DataType::kFLOAT) {
            if (error) {
                *error = "MultiTensor3Input RIFE variant requires kFLOAT input, got " +
                         dtypeToString(inType);
            }
            return false;
        }
    }

    // Phase 9 (RC-4): adaptive shape-profile selection. Static engines obey
    // their hard-coded dims; profiled engines use the 64-aligned size when it
    // falls inside [min, max] and the exact source size otherwise; engines
    // with neither default to the 64-aligned size. The padded dimensions are
    // finalized here, BEFORE any device allocation or tensor binding.
    constexpr uint32_t kAlign = 64;
    const uint32_t aligned_w = ((width + kAlign - 1) / kAlign) * kAlign;
    const uint32_t aligned_h = ((height + kAlign - 1) / kAlign) * kAlign;
    padded_width_ = aligned_w;
    padded_height_ = aligned_h;

    bool shape_resolved = false;
    const nvinfer1::Dims tensor_dims = impl_->engine->getTensorShape(check_tensor_name);
    if (tensor_dims.nbDims == 4 && tensor_dims.d[2] > 0 && tensor_dims.d[3] > 0) {
        padded_height_ = static_cast<uint32_t>(tensor_dims.d[2]);
        padded_width_ = static_cast<uint32_t>(tensor_dims.d[3]);
        shape_resolved = true;
    }
    if (!shape_resolved && impl_->engine->getNbOptimizationProfiles() > 0) {
        const nvinfer1::Dims min_d = impl_->engine->getProfileShape(
            check_tensor_name, 0, nvinfer1::OptProfileSelector::kMIN);
        const nvinfer1::Dims max_d = impl_->engine->getProfileShape(
            check_tensor_name, 0, nvinfer1::OptProfileSelector::kMAX);
        if (min_d.nbDims == 4 && max_d.nbDims == 4) {
            // TRT 10.13 Dims::d is int64_t; widen the lambda to match so no
            // narrowing argument conversion occurs (MSVC C4244 under /WX).
            const auto fits = [](int64_t lo, int64_t hi, int64_t v) {
                return v >= lo && v <= hi;
            };
            padded_height_ = fits(min_d.d[2], max_d.d[2], aligned_h) ? aligned_h
                                                                     : static_cast<uint32_t>(height);
            padded_width_ = fits(min_d.d[3], max_d.d[3], aligned_w) ? aligned_w
                                                                    : static_cast<uint32_t>(width);
            shape_resolved = true;
        }
    }
    if (!shape_resolved) {
        // No static dims and no usable profile: keep the 64-aligned default.
    }

    // Allocate GPU buffers. Phase 9.1 (RW-P9-02): the preprocess always writes
    // IEEE-754 binary32 into d_input_fp32_scratch and the postprocess always
    // reads IEEE-754 binary32 from d_output_fp32_scratch; kHALF engine I/O
    // adds half-precision tensor buffers plus an explicit conversion chain
    // around enqueueV3. For kFLOAT engines the tensors alias the scratch
    // (zero-copy).
    const size_t plane_pixels = static_cast<size_t>(padded_width_) * padded_height_;

    if (variant_ == Variant::SingleTensor7Ch) {
        impl_->input_bytes = 7 * plane_pixels * sizeof(float);
        impl_->output_bytes = 3 * plane_pixels * sizeof(float);
        if (g_cuda.mem_alloc(&impl_->d_input_fp32_scratch, impl_->input_bytes) != 0) {
            if (error) *error = "Failed to allocate 7-channel fp32 scratch buffer on GPU";
            return false;
        }
        if (g_cuda.mem_alloc(&impl_->d_output_fp32_scratch, impl_->output_bytes) != 0) {
            if (error) *error = "Failed to allocate 3-channel fp32 scratch buffer on GPU";
            return false;
        }

        if (impl_->in_dtype == nvinfer1::DataType::kHALF) {
            const size_t half_input_bytes = 7 * plane_pixels * 2u;
            if (g_cuda.mem_alloc(&impl_->d_input_tensor, half_input_bytes) != 0) {
                if (error) *error = "Failed to allocate 7-channel kHALF input tensor buffer on GPU";
                return false;
            }
        } else {
            impl_->d_input_tensor = impl_->d_input_fp32_scratch;
        }
        if (impl_->out_dtype == nvinfer1::DataType::kHALF) {
            const size_t half_output_bytes = 3 * plane_pixels * 2u;
            if (g_cuda.mem_alloc(&impl_->d_output_tensor, half_output_bytes) != 0) {
                if (error) *error = "Failed to allocate 3-channel kHALF output tensor buffer on GPU";
                return false;
            }
        } else {
            impl_->d_output_tensor = impl_->d_output_fp32_scratch;
        }

        // Set input shape for dynamic axes (strictly validated, RC-4)
        nvinfer1::Dims4 dims{1, 7, static_cast<int32_t>(padded_height_), static_cast<int32_t>(padded_width_)};
        if (!impl_->context->setInputShape(impl_->in_name.c_str(), dims)) {
            if (error) {
                *error = "TensorRT setInputShape failed for shape [1, 7, " +
                         std::to_string(padded_height_) + ", " + std::to_string(padded_width_) + "]";
            }
            return false;
        }

        impl_->context->setTensorAddress(impl_->in_name.c_str(), reinterpret_cast<void*>(impl_->d_input_tensor));
        impl_->context->setTensorAddress(impl_->out_name.c_str(), reinterpret_cast<void*>(impl_->d_output_tensor));
    } else {
        impl_->input_bytes = 3 * plane_pixels * sizeof(float);
        impl_->output_bytes = 3 * plane_pixels * sizeof(float);
        if (g_cuda.mem_alloc(&impl_->d_output_tensor, impl_->output_bytes) != 0) {
            if (error) *error = "Failed to allocate output tensor buffer on GPU";
            return false;
        }
        const size_t img_bytes = 3 * plane_pixels * sizeof(float);
        if (g_cuda.mem_alloc(&impl_->d_img0_tensor, img_bytes) != 0 ||
            g_cuda.mem_alloc(&impl_->d_img1_tensor, img_bytes) != 0) {
            if (error) *error = "Failed to allocate separate input tensor buffers on GPU";
            return false;
        }

        nvinfer1::Dims4 dims{1, 3, static_cast<int32_t>(padded_height_), static_cast<int32_t>(padded_width_)};
        if (!impl_->context->setInputShape(impl_->img0_name.c_str(), dims) ||
            !impl_->context->setInputShape(impl_->img1_name.c_str(), dims)) {
            if (error) {
                *error = "TensorRT setInputShape failed for shape [1, 3, " +
                         std::to_string(padded_height_) + ", " + std::to_string(padded_width_) + "]";
            }
            return false;
        }

        impl_->context->setTensorAddress(impl_->img0_name.c_str(), reinterpret_cast<void*>(impl_->d_img0_tensor));
        impl_->context->setTensorAddress(impl_->img1_name.c_str(), reinterpret_cast<void*>(impl_->d_img1_tensor));

        if (!impl_->timestep_name.empty()) {
            const size_t ts_bytes = plane_pixels * sizeof(float);
            (void)g_cuda.mem_alloc(&impl_->d_timestep_tensor, ts_bytes);
            nvinfer1::Dims4 ts_dims{1, 1, static_cast<int32_t>(padded_height_), static_cast<int32_t>(padded_width_)};
            if (!impl_->context->setInputShape(impl_->timestep_name.c_str(), ts_dims)) {
                if (error) {
                    *error = "TensorRT setInputShape failed for timestep shape [1, 1, " +
                             std::to_string(padded_height_) + ", " + std::to_string(padded_width_) + "]";
                }
                return false;
            }
            impl_->context->setTensorAddress(impl_->timestep_name.c_str(), reinterpret_cast<void*>(impl_->d_timestep_tensor));
        }

        impl_->context->setTensorAddress(impl_->out_name.c_str(), reinterpret_cast<void*>(impl_->d_output_tensor));
    }

    if (g_cuda.mem_alloc(&impl_->d_sad_sum, sizeof(uint64_t)) != 0) {
        if (error) *error = "Failed to allocate SAD device accumulator (uint64_t)";
        return false;
    }

    // Phase 9 (RC-4): mandatory alpha-sensitivity probe for SingleTensor7Ch
    // engines. Rejects any model whose output ignores the ch6 timestep channel
    // (fixed-timestep exports / static midpoint models) before it can ever
    // emit silently frozen interpolation frames.
    if (variant_ == Variant::SingleTensor7Ch) {
        auto run_alpha_probe = [&]() -> std::string {
            constexpr uint32_t kProbeSize = 256u;
            constexpr uint32_t kProbePitch = kProbeSize;

            if (!g_cuda.mem_alloc_host || !g_cuda.mem_free_host || !g_cuda.memcpy_htod ||
                !g_cuda.memset_d32_async || !g_cuda.memcpy_dtoh || !g_cuda.stream_sync ||
                impl_->d_input_fp32_scratch == 0 || impl_->d_output_fp32_scratch == 0 ||
                impl_->d_input_tensor == 0 || impl_->d_output_tensor == 0 || impl_->d_sad_sum == 0) {
                return "RIFE alpha-sensitivity probe unavailable: missing CUDA driver APIs or buffers";
            }

            const size_t probe_nv12_bytes = static_cast<size_t>(kProbePitch) * kProbeSize * 3u / 2u;
            const size_t padded_plane_pixels = static_cast<size_t>(padded_width_) * padded_height_;
            const size_t probe_out_bytes = 3 * padded_plane_pixels * sizeof(float);

            unsigned char* host_prev = nullptr;
            unsigned char* host_curr = nullptr;
            float* host_out_a = nullptr;
            float* host_out_b = nullptr;
            CUdeviceptr dev_prev = 0;
            CUdeviceptr dev_curr = 0;
            std::string probe_error;
            bool ok = false;

            do {
                if (g_cuda.mem_alloc_host(reinterpret_cast<void**>(&host_prev), probe_nv12_bytes) != 0 ||
                    g_cuda.mem_alloc_host(reinterpret_cast<void**>(&host_curr), probe_nv12_bytes) != 0 ||
                    g_cuda.mem_alloc_host(reinterpret_cast<void**>(&host_out_a), probe_out_bytes) != 0 ||
                    g_cuda.mem_alloc_host(reinterpret_cast<void**>(&host_out_b), probe_out_bytes) != 0) {
                    probe_error = "RIFE alpha-sensitivity probe: cuMemAllocHost failed";
                    break;
                }
                std::memset(host_prev, 0x10, static_cast<size_t>(kProbePitch) * kProbeSize);
                std::memset(host_prev + static_cast<size_t>(kProbePitch) * kProbeSize, 0x80,
                            probe_nv12_bytes - static_cast<size_t>(kProbePitch) * kProbeSize);
                std::memset(host_curr, 0xE0, static_cast<size_t>(kProbePitch) * kProbeSize);
                std::memset(host_curr + static_cast<size_t>(kProbePitch) * kProbeSize, 0x80,
                            probe_nv12_bytes - static_cast<size_t>(kProbePitch) * kProbeSize);

                if (g_cuda.mem_alloc(&dev_prev, probe_nv12_bytes) != 0 ||
                    g_cuda.mem_alloc(&dev_curr, probe_nv12_bytes) != 0) {
                    probe_error = "RIFE alpha-sensitivity probe: cuMemAlloc failed";
                    break;
                }
                if (g_cuda.memcpy_htod(dev_prev, host_prev, probe_nv12_bytes) != 0 ||
                    g_cuda.memcpy_htod(dev_curr, host_curr, probe_nv12_bytes) != 0) {
                    probe_error = "RIFE alpha-sensitivity probe: cuMemcpyHtoD failed";
                    break;
                }

                // Reset SAD (64-bit: 2x 32-bit dwords), then run one full preprocess to populate ch0..5
                // into the fp32 scratch. The grid covers the padded engine dims; source coords clamp
                // into the 256x256 test pair (edge replication), keeping plane
                // strides consistent with the bound engine input shape.
                if (g_cuda.memset_d32_async(impl_->d_sad_sum, 0, 2, nullptr) != 0) {
                    probe_error = "RIFE alpha-sensitivity probe: SAD reset failed";
                    break;
                }
                const RifePreprocessLaunch launch{
                    .y_plane_0 = dev_prev,
                    .uv_plane_0 = 0,
                    .y_plane_1 = dev_curr,
                    .uv_plane_1 = 0,
                    .output_tensor = impl_->d_input_fp32_scratch,
                    .width = kProbeSize,
                    .height = kProbeSize,
                    .y_pitch = kProbePitch,
                    .uv_pitch = kProbePitch,
                    .padded_width = padded_width_,
                    .padded_height = padded_height_,
                    .alpha = 0.2f,
                    .full_range = 0,
                    .d_sad_sum = impl_->d_sad_sum,
                };
                if (launchRifePreprocess(launch, nullptr) != RTX_OK) {
                    probe_error = "RIFE alpha-sensitivity probe: preprocess kernel launch failed";
                    break;
                }
                if (impl_->in_dtype == nvinfer1::DataType::kHALF) {
                    // RW-P9-02: convert the fp32 scratch to the half-precision
                    // engine input before inference.
                    const int32_t cvt_res = launchFp32ToFp16(
                        impl_->d_input_fp32_scratch, impl_->d_input_tensor,
                        static_cast<uint32_t>(7 * padded_plane_pixels), nullptr);
                    if (cvt_res != RTX_OK) {
                        probe_error = "RIFE alpha-sensitivity probe: fp32->fp16 input conversion failed";
                        break;
                    }
                }
                g_cuda.stream_sync(nullptr);

                auto infer_at_alpha = [&](float alpha) -> bool {
                    const uint64_t ch6_addr = impl_->d_input_fp32_scratch + 6 * padded_plane_pixels * sizeof(float);
                    uint32_t alpha_bits = 0;
                    std::memcpy(&alpha_bits, &alpha, sizeof(alpha_bits));
                    if (g_cuda.memset_d32_async(ch6_addr, alpha_bits, padded_plane_pixels, nullptr) != 0) {
                        probe_error = "RIFE alpha-sensitivity probe: ch6 memset failed";
                        return false;
                    }
                    if (impl_->in_dtype == nvinfer1::DataType::kHALF) {
                        // RW-P9-02: refresh only the ch6 plane of the
                        // half-precision engine input.
                        const uint64_t fp16_ch6 = impl_->d_input_tensor + 6 * padded_plane_pixels * sizeof(uint16_t);
                        if (launchFp32ToFp16(ch6_addr, fp16_ch6,
                                             static_cast<uint32_t>(padded_plane_pixels), nullptr) != RTX_OK) {
                            probe_error = "RIFE alpha-sensitivity probe: ch6 fp32->fp16 conversion failed";
                            return false;
                        }
                    }
                    if (!impl_->context->enqueueV3(nullptr)) {
                        probe_error = "RIFE alpha-sensitivity probe: enqueueV3 failed";
                        return false;
                    }
                    if (g_cuda.stream_sync(nullptr) != 0) {
                        probe_error = "RIFE alpha-sensitivity probe: stream sync failed";
                        return false;
                    }
                    return true;
                };

                float* out_targets[2] = {host_out_a, host_out_b};
                const float alphas[2] = {0.2f, 0.8f};
                bool infer_ok = true;
                for (int i = 0; i < 2 && infer_ok; ++i) {
                    infer_ok = infer_at_alpha(alphas[i]);
                    if (infer_ok) {
                        uint64_t readback_addr = impl_->d_output_fp32_scratch;
                        if (impl_->out_dtype == nvinfer1::DataType::kHALF) {
                            // RW-P9-02: convert the half-precision output back
                            // to fp32 before host readback (never reinterpret
                            // half data as float).
                            const int32_t cvt_res = launchFp16ToFp32(
                                impl_->d_output_tensor, impl_->d_output_fp32_scratch,
                                static_cast<uint32_t>(3 * padded_plane_pixels), nullptr);
                            if (cvt_res != RTX_OK) {
                                if (probe_error.empty()) {
                                    probe_error = "RIFE alpha-sensitivity probe: fp16->fp32 output conversion failed";
                                }
                                infer_ok = false;
                                break;
                            }
                        }
                        infer_ok = (g_cuda.memcpy_dtoh(out_targets[i], readback_addr, probe_out_bytes) == 0);
                        if (!infer_ok && probe_error.empty()) {
                            probe_error = "RIFE alpha-sensitivity probe: DtoH copy failed";
                        }
                    }
                }
                if (!infer_ok) break;

                double l1 = 0.0;
                const size_t out_floats = 3 * padded_plane_pixels;
                for (size_t i = 0; i < out_floats; ++i) {
                    l1 += std::fabs(static_cast<double>(host_out_a[i]) - static_cast<double>(host_out_b[i]));
                }
                if (l1 == 0.0) {
                    probe_error = "engine ignores timestep channel (alpha): outputs identical at alpha 0.2 vs 0.8";
                    break;
                }

                impl_->alpha_probe_passed = true;
                ok = true;
            } while (false);

            if (dev_prev) g_cuda.mem_free(dev_prev);
            if (dev_curr) g_cuda.mem_free(dev_curr);
            if (host_prev) g_cuda.mem_free_host(host_prev);
            if (host_curr) g_cuda.mem_free_host(host_curr);
            if (host_out_a) g_cuda.mem_free_host(host_out_a);
            if (host_out_b) g_cuda.mem_free_host(host_out_b);

            // The probe consumed the input tensor; the frame-pair cache must
            // start empty so the first real interpolate() performs a full
            // preprocess.
            impl_->resetFramePairCacheState();
            return ok ? std::string() : probe_error;
        };

        const std::string probe_err = run_alpha_probe();
        if (!probe_err.empty()) {
            if (error) *error = probe_err;
            // Leave no half-loaded engine behind.
            impl_->releaseBuffers();
            if (impl_->context) { delete impl_->context; impl_->context = nullptr; }
            if (impl_->engine) { delete impl_->engine; impl_->engine = nullptr; }
            if (impl_->runtime) { delete impl_->runtime; impl_->runtime = nullptr; }
            return false;
        }
    }

    return true;
}

int32_t RifeEngine::interpolate(
    uint64_t prev_nv12,
    uint64_t curr_nv12,
    uint32_t pitch,
    float alpha,
    uint64_t out_nv12,
    int32_t full_range,
    int32_t* scene_cut,
    void* stream) {

    if (!impl_ || !impl_->context || prev_nv12 == 0 || curr_nv12 == 0 || out_nv12 == 0 || pitch == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }

    // Every executable interpolate call increments exactly one of
    // preprocess_launches (full preprocess) or cache_reuse (fast path), so the
    // [RIFE-STATS] invariant interpolate == preprocess + cache_reuse holds.
    ++impl_->interpolate_calls;

    if (scene_cut) *scene_cut = 0;

    const size_t total_nv12_bytes = static_cast<size_t>(pitch) * height_ * 3u / 2u;
    const size_t plane_pixels = static_cast<size_t>(padded_width_) * padded_height_;

    // Phase 9 (RC-5) frame-pair cache: at 120fps (1:5 expansion) the same
    // (prev, curr) pair is interpolated at alpha = 0.2/0.4/0.6/0.8. When the
    // pointer pair matches the cached pair, the six image channels are already
    // resident in d_input_tensor and the SAD was already synced, so only the
    // ch6 alpha plane needs a cuMemsetD32Async update. The scheduler holds the
    // prev slot lease across the FRUC window, so adjacent windows always carry
    // a different prev pointer; plain pointer identity can never false-hit.
    // No content hashing by design.
    bool reuse = false;
    int32_t effective_scene_cut = 0;
    if (variant_ == Variant::SingleTensor7Ch && impl_->cache_valid &&
        prev_nv12 == impl_->last_prev_nv12 && curr_nv12 == impl_->last_curr_nv12 &&
        pitch == impl_->last_pitch && impl_->d_input_fp32_scratch != 0 && g_cuda.memset_d32_async) {
        const uint64_t fp32_ch6 = impl_->d_input_fp32_scratch + 6ULL * plane_pixels * sizeof(float);
        uint32_t alpha_bits = 0;
        std::memcpy(&alpha_bits, &alpha, sizeof(alpha_bits));
        bool queued = false;
        if (impl_->in_dtype == nvinfer1::DataType::kHALF && impl_->d_input_tensor != 0) {
            // RW-P9-02: kHALF engines must refresh ch6 too — update the fp32
            // scratch plane, then convert just that plane into the
            // half-precision engine input.
            const uint64_t fp16_ch6 = impl_->d_input_tensor + 6ULL * plane_pixels * sizeof(uint16_t);
            if (g_cuda.memset_d32_async(fp32_ch6, alpha_bits, plane_pixels, stream) == 0) {
                queued = (launchFp32ToFp16(fp32_ch6, fp16_ch6,
                                           static_cast<uint32_t>(plane_pixels), stream) == RTX_OK);
            }
        } else {
            queued = (g_cuda.memset_d32_async(fp32_ch6, alpha_bits, plane_pixels, stream) == 0);
        }
        if (queued) {
            ++impl_->cache_reuse;
            reuse = true;
            effective_scene_cut = impl_->last_scene_cut;
        }
        // Fast-path queue failure silently falls through to the full preprocess.
    }

    if (!reuse) {
        ++impl_->preprocess_launches;

        RtxResult prep_res = RTX_OK;
        if (variant_ == Variant::SingleTensor7Ch) {
            // Reset 64-bit SAD accumulator (2x 32-bit dwords) for this pair's preprocess.
            if (impl_->d_sad_sum && g_cuda.memset_d32_async) {
                g_cuda.memset_d32_async(impl_->d_sad_sum, 0, 2, stream);
            }
            // The preprocess always writes IEEE-754 binary32 into the scratch.
            const RifePreprocessLaunch launch{
                .y_plane_0 = prev_nv12,
                .uv_plane_0 = 0,
                .y_plane_1 = curr_nv12,
                .uv_plane_1 = 0,
                .output_tensor = impl_->d_input_fp32_scratch,
                .width = width_,
                .height = height_,
                .y_pitch = pitch,
                .uv_pitch = pitch,
                .padded_width = padded_width_,
                .padded_height = padded_height_,
                .alpha = alpha,
                .full_range = full_range,
                .d_sad_sum = impl_->d_sad_sum,
            };
            prep_res = launchRifePreprocess(launch, stream);
            if (prep_res == RTX_OK && impl_->in_dtype == nvinfer1::DataType::kHALF) {
                // RW-P9-02: convert the fresh fp32 tensor into the
                // half-precision engine input (full 7-plane sweep).
                prep_res = launchFp32ToFp16(
                    impl_->d_input_fp32_scratch, impl_->d_input_tensor,
                    static_cast<uint32_t>(7 * plane_pixels), stream);
            }
        } else {
            // MultiTensor3Input variant: keep detection/loading, loud-fail at
            // interpolate time (no Phase 9 kernel for this variant).
            if (impl_->d_sad_sum && g_cuda.memset_d32_async) {
                g_cuda.memset_d32_async(impl_->d_sad_sum, 0, 2, stream);
            }
            const RifePreprocessSeparateLaunch launch{
                .y_plane_0 = prev_nv12,
                .uv_plane_0 = prev_nv12 + static_cast<uint64_t>(pitch) * height_,
                .y_plane_1 = curr_nv12,
                .uv_plane_1 = curr_nv12 + static_cast<uint64_t>(pitch) * height_,
                .img0_tensor = impl_->d_img0_tensor,
                .img1_tensor = impl_->d_img1_tensor,
                .timestep_tensor = impl_->d_timestep_tensor,
                .width = width_,
                .height = height_,
                .y_pitch = pitch,
                .uv_pitch = pitch,
                .padded_width = padded_width_,
                .padded_height = padded_height_,
                .alpha = alpha,
                .full_range = full_range,
                .d_sad_sum = impl_->d_sad_sum,
            };
            prep_res = launchRifePreprocessSeparate(launch, stream);
        }

        if (prep_res != RTX_OK) {
            return prep_res;
        }

        // Scene cut detection: synchronize and read 64-bit SAD once per pair.
        if (impl_->d_sad_sum && g_cuda.stream_sync && g_cuda.memcpy_dtoh) {
            g_cuda.stream_sync(stream);
            uint64_t host_sad = 0;
            g_cuda.memcpy_dtoh(&host_sad, impl_->d_sad_sum, sizeof(uint64_t));
            const double total_pixels = static_cast<double>(width_) * static_cast<double>(height_);
            const float diff_norm = (total_pixels > 0.0)
                ? static_cast<float>(static_cast<double>(host_sad) / (total_pixels * 255.0))
                : 0.0f;
            effective_scene_cut = (diff_norm > 0.35f) ? 1 : 0;
        }

        if (variant_ == Variant::SingleTensor7Ch) {
            impl_->last_prev_nv12 = prev_nv12;
            impl_->last_curr_nv12 = curr_nv12;
            impl_->last_pitch = pitch;
            impl_->last_scene_cut = effective_scene_cut;
            impl_->cache_valid = true;
        }
    }

    if (scene_cut) *scene_cut = effective_scene_cut;

    // Scene hard cut / fallback: nearest-neighbor phase step, alpha < 0.5 ->
    // prev frame, alpha >= 0.5 -> curr frame (device-to-device copy).
    if (effective_scene_cut != 0) {
        const uint64_t src = (alpha < 0.5f) ? prev_nv12 : curr_nv12;
        if (g_cuda.memcpy_dtod_async) {
            g_cuda.memcpy_dtod_async(out_nv12, src, total_nv12_bytes, stream);
        }
        return RTX_OK;
    }

    // Run RIFE neural network inference on stream
    const bool infer_ok = impl_->context->enqueueV3(reinterpret_cast<cudaStream_t>(stream));
    if (!infer_ok) {
        // Fallback: copy nearest source frame on inference failure.
        const uint64_t src = (alpha < 0.5f) ? prev_nv12 : curr_nv12;
        if (g_cuda.memcpy_dtod_async) {
            g_cuda.memcpy_dtod_async(out_nv12, src, total_nv12_bytes, stream);
        }
        return RTX_ERR_INTERNAL;
    }

    // Launch postprocessing kernel. Phase 9.1 (RW-P9-02): the postprocess
    // kernel reads IEEE-754 binary32 only — kHALF engine output must be
    // converted back to the fp32 scratch first (never reinterpret half data).
    uint64_t post_rgb_tensor = impl_->d_output_fp32_scratch;
    if (impl_->out_dtype == nvinfer1::DataType::kHALF) {
        const size_t out_elems = 3ULL * static_cast<size_t>(padded_width_) * static_cast<size_t>(padded_height_);
        const int32_t cvt_res = launchFp16ToFp32(
            impl_->d_output_tensor, impl_->d_output_fp32_scratch,
            static_cast<uint32_t>(out_elems), stream);
        if (cvt_res != RTX_OK) {
            return cvt_res;
        }
    }

    const RifePostprocessLaunch postLaunch{
        .rgb_tensor = post_rgb_tensor,
        .y_plane = out_nv12,
        .uv_plane = 0,
        .width = width_,
        .height = height_,
        .y_pitch = pitch,
        .uv_pitch = pitch,
        .padded_width = padded_width_,
        .padded_height = padded_height_,
        .full_range = full_range,
    };

    return launchRifePostprocess(postLaunch, stream);
}

bool RifeEngine::hasValidAlphaResponse() const noexcept {
    return impl_ && impl_->alpha_probe_passed;
}

void RifeEngine::resetFramePairCache() noexcept {
    if (impl_) {
        impl_->resetFramePairCacheState();
    }
}

}  // namespace rtx_ngx
