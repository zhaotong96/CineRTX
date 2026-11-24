/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT 10.13.3.9 Practical-RIFE v4.25 inference engine.
 * Phase 12 (Wave 12.2-B1): eliminated RifeEngine::Impl and duplicate
 * RifeTrtLogger, removed GPLv3 include/rtx/shot_segmenter.h dependency, and
 * migrated to nv_cuda::* / trt_loader::* / tuple-based PTX launch calls.
 */

#include "rife_engine.h"
#include "ngx_bridge_abi.h"
#include "nv_func_tables.h"
#include "rife_pack_runtime.h"
#include "trt_loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <tuple>
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

bool isDirtyEnginePlaceholder(const std::filesystem::path& candidate) {
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return false;
    const auto fsize = std::filesystem::file_size(candidate, ec);
    if (ec || candidate.filename() == "rife_builtin_synth.engine" || fsize >= 512u * 1024u) return false;
    std::fprintf(stderr, "[RIFE-ENGINE] Ignoring dirty placeholder engine %s (%llu bytes < 512 KiB)\n",
                 candidate.string().c_str(), static_cast<unsigned long long>(fsize));
    return true;
}

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

/* MIT-native 8x8 block-SAD shot-boundary detector (eliminates GPLv3 header dependency). */
bool evalBlockSadSceneCut(const std::array<uint32_t, 64>& block_sad,
                          uint32_t width, uint32_t height, float& ema_motion) noexcept {
    if (width == 0 || height == 0) return false;
    uint64_t total_sad = 0;
    for (uint32_t s : block_sad) total_sad += s;
    const double total_denom = static_cast<double>(width) * static_cast<double>(height) * 255.0;
    const float mu_k = static_cast<float>(static_cast<double>(total_sad) / total_denom);
    const double block_denom = (std::max)(1.0, static_cast<double>(width) / 8.0) *
                               (std::max)(1.0, static_cast<double>(height) / 8.0) * 255.0;
    int outliers = 0;
    for (uint32_t s : block_sad) {
        if (static_cast<float>(static_cast<double>(s) / block_denom) > 0.16f) ++outliers;
    }
    const float p_outlier = static_cast<float>(outliers) / 64.0f;
    const float baseline = (std::max)(ema_motion, 0.025f);
    const bool hard_cut = (mu_k > 0.22f) ||
                          (mu_k > 0.085f && mu_k > 3.2f * baseline && p_outlier >= 0.30f) ||
                          (p_outlier >= 0.375f && mu_k > 2.4f * ema_motion);
    if (hard_cut) {
        ema_motion = 0.03f;
    } else {
        constexpr float kAlpha = 2.0f / 9.0f;
        ema_motion = (1.0f - kAlpha) * ema_motion + kAlpha * mu_k;
    }
    return hard_cut;
}

}  // namespace

RifeEngine::~RifeEngine() {
    releaseBuffers();
    std::fprintf(stderr, "[RIFE-STATS] preprocess=%u interpolate=%u cache_reuse=%u\n",
                 preprocess_launches_, interpolate_calls_, cache_reuse_);
    releaseTrtObjects();
}

void RifeEngine::releaseTrtObjects() noexcept {
    if (context_) { delete static_cast<nvinfer1::IExecutionContext*>(context_); context_ = nullptr; }
    if (engine_) { delete static_cast<nvinfer1::ICudaEngine*>(engine_); engine_ = nullptr; }
    if (runtime_) { delete static_cast<nvinfer1::IRuntime*>(runtime_); runtime_ = nullptr; }
}

void RifeEngine::resetFramePairCache() noexcept {
    last_prev_nv12_ = 0;
    last_curr_nv12_ = 0;
    last_pitch_ = 0;
    last_scene_cut_ = 0;
    cache_valid_ = false;
}

void RifeEngine::releaseBuffers() noexcept {
    if (!nv_cuda::loaded()) return;
    if (d_input_tensor_ && d_input_tensor_ != d_input_fp32_scratch_) { (void)nv_cuda::memFree(d_input_tensor_); }
    if (d_output_tensor_ && d_output_tensor_ != d_output_fp32_scratch_) { (void)nv_cuda::memFree(d_output_tensor_); }
    d_input_tensor_ = 0;
    d_output_tensor_ = 0;
    for (uint64_t* ptr : {&d_input_fp32_scratch_, &d_output_fp32_scratch_, &d_img0_tensor_,
                          &d_img1_tensor_, &d_timestep_tensor_, &d_sad_sum_, &d_block_sad_}) {
        if (*ptr) { (void)nv_cuda::memFree(*ptr); *ptr = 0; }
    }
    input_bytes_ = 0;
    output_bytes_ = 0;
    resetFramePairCache();
}

bool RifeEngine::loadEngine(const char* engine_path, uint32_t width, uint32_t height, std::string* error) {
    return loadEngineEx(engine_path, width, height, 1.0f, 128u, true, nullptr, error);
}

bool RifeEngine::loadEngineEx(const char* engine_path, uint32_t width, uint32_t height,
                              float scale, uint32_t align_mod, bool reflect_padding,
                              uint64_t* out_trt_peak_bytes, std::string* error) {
    scale_ = (scale > 0.0f && scale <= 1.0f) ? scale : 1.0f;
    align_mod_ = (align_mod >= 64u && (align_mod & (align_mod - 1u)) == 0u) ? align_mod : 128u;
    reflect_padding_ = reflect_padding;
    trt_peak_allocation_bytes_ = 0;
    if (!engine_path || width == 0 || height == 0) {
        if (error) *error = "Invalid engine path or dimensions";
        return false;
    }
    if (!nv_cuda::loaded()) {
        if (error) *error = "Failed to initialize CUDA driver APIs for RIFE";
        return false;
    }
    if (!trt_loader::loadRuntime(error)) return false;
    if (!isRifePackAvailable()) {
        if (error) *error = "RIFE PTX pre/post-processing module failed to JIT compile: " + std::string(rifePackLastError());
        return false;
    }

    std::filesystem::path resolved_engine(engine_path);
    std::error_code ec;
    if (isDirtyEnginePlaceholder(resolved_engine)) resolved_engine.clear();
    ec.clear();

    bool found_existing = !resolved_engine.empty() &&
                          std::filesystem::exists(resolved_engine, ec) &&
                          std::filesystem::file_size(resolved_engine, ec) > 0;
    const bool is_portrait = (height > width && width > 0);
    const std::string preferred_name = is_portrait ? "rife_v4.25_portrait.engine" : "rife_v4.25_landscape.engine";

    if (!found_existing) {
        std::vector<std::filesystem::path> base_dirs;
        std::filesystem::path primary_exe_dir, shared_ext_models_dir;
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
                if (shared_ext_models_dir.empty() && std::filesystem::exists(anc / "external" / "models", ec)) {
                    shared_ext_models_dir = anc / "external" / "models";
                }
            }
        }
#endif
        if (const auto cwd = std::filesystem::current_path(ec); !cwd.empty()) {
            base_dirs.push_back(cwd);
            base_dirs.push_back(cwd / "models");
            base_dirs.push_back(cwd / ".vs" / "build" / "external" / "models");
            if (shared_ext_models_dir.empty() && std::filesystem::exists(cwd / ".vs" / "build" / "external" / "models", ec)) {
                shared_ext_models_dir = cwd / ".vs" / "build" / "external" / "models";
            }
        }
        constexpr uint64_t kMaxEngineBytes = 35ull * 1024ull * 1024ull;
        constexpr uint64_t kLegacyMinBytes = 20ull * 1024ull * 1024ull;
        constexpr uint64_t kLegacyMaxBytes = 32ull * 1024ull * 1024ull;

        for (const auto& dir : base_dirs) {
            const auto candidate = dir / preferred_name;
            const auto fsz = std::filesystem::exists(candidate, ec) ? std::filesystem::file_size(candidate, ec) : 0ull;
            if (fsz > 0 && fsz <= kMaxEngineBytes && !isDirtyEnginePlaceholder(candidate)) {
                resolved_engine = candidate;
                found_existing = true;
                break;
            }
        }
        if (!found_existing && !is_portrait) {
            for (const auto& dir : base_dirs) {
                const auto candidate = dir / "rife_v4.25.engine";
                const auto fsz = std::filesystem::exists(candidate, ec) ? std::filesystem::file_size(candidate, ec) : 0ull;
                if (fsz >= kLegacyMinBytes && fsz <= kLegacyMaxBytes && !isDirtyEnginePlaceholder(candidate)) {
                    resolved_engine = candidate;
                    found_existing = true;
                    break;
                }
            }
        }
        if (!found_existing) {
            std::string onnx_source;
            for (const auto& dir : base_dirs) {
                for (const char* onnx_name : {"rife_v4.25.onnx", "rife_v4.25_v2.onnx",
                                              "rife_v4.25_heavy.onnx", "rife.onnx",
                                              "model.onnx", "rife_v2/rife_v4.25.onnx"}) {
                    const auto candidate = dir / onnx_name;
                    if (std::filesystem::exists(candidate, ec) && std::filesystem::file_size(candidate, ec) > 256u) {
                        onnx_source = candidate.string();
                        break;
                    }
                }
                if (!onnx_source.empty()) break;
            }
            if (onnx_source.empty()) {
                if (error) {
                    *error = "No valid RIFE engine (.engine) or ONNX model found; builtin synthetic fallback is disabled.";
                }
                return false;
            }
            if (resolved_engine.empty() || resolved_engine.is_relative()) {
                const auto out_base = !primary_exe_dir.empty()
                                          ? primary_exe_dir
                                          : (!base_dirs.empty() ? base_dirs.front() : std::filesystem::path("."));
                resolved_engine = out_base / "models" / preferred_name;
            }
            char build_err[512] = {};
            const std::string out_engine_str = resolved_engine.string();
            if (rtx_ngx_bridge_build_rife_engine(onnx_source.c_str(), out_engine_str.c_str(),
                                                 width, height, build_err, sizeof(build_err)) != RTX_OK) {
                if (error) *error = std::string("Auto-build RIFE engine failed: ") + build_err;
                return false;
            }
            if (!shared_ext_models_dir.empty()) {
                std::filesystem::create_directories(shared_ext_models_dir, ec);
                std::filesystem::copy_file(resolved_engine, shared_ext_models_dir / preferred_name,
                                           std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }
    if (resolved_engine.filename() == "rife_v4.25.engine") {
        const auto sibling = resolved_engine.parent_path() / preferred_name;
        std::error_code sib_ec;
        if (std::filesystem::exists(sibling, sib_ec) &&
            std::filesystem::file_size(sibling, sib_ec) > 0 &&
            std::filesystem::file_size(sibling, sib_ec) <= 35ull * 1024ull * 1024ull) {
            resolved_engine = sibling;
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
    if (static_cast<uint64_t>(size) > 35ull * 1024ull * 1024ull) {
        if (error) {
            *error = "Rejected polluted RIFE engine (>35 MB, size=" +
                     std::to_string(static_cast<uint64_t>(size)) + " bytes): " + resolved_engine.string();
        }
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size)) {
        if (error) *error = "Failed to read RIFE engine file into memory";
        return false;
    }

    releaseBuffers();
    releaseTrtObjects();

    auto* runtime = static_cast<nvinfer1::IRuntime*>(trt_loader::createInferRuntime(trt_loader::rifeTrtLogger()));
    if (!runtime) {
        if (error) *error = "createInferRuntime failed";
        return false;
    }
    runtime_ = runtime;
    builtin_synth_ = (buffer.size() < 2u * 1024u * 1024u);
    auto* engine = runtime->deserializeCudaEngine(buffer.data(), buffer.size());
    if (!engine) {
        if (error) *error = "Failed to deserialize CUDA engine (SM architecture mismatch?)";
        return false;
    }
    engine_ = engine;
    auto* context = engine->createExecutionContext();
    if (!context) {
        if (error) *error = "Failed to create execution context for RIFE engine";
        return false;
    }
    context_ = context;
    width_ = width;
    height_ = height;

    int inCount = 0;
    for (int32_t i = 0; i < engine->getNbIOTensors(); ++i) {
        const char* name = engine->getIOTensorName(i);
        const auto mode = engine->getTensorIOMode(name);
        if (mode == nvinfer1::TensorIOMode::kINPUT) {
            inCount++;
            if (in_name_.empty()) in_name_ = name;
            std::string s(name);
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (s.find("0") != std::string::npos || s.find("prev") != std::string::npos) img0_name_ = name;
            else if (s.find("1") != std::string::npos || s.find("curr") != std::string::npos) img1_name_ = name;
            else if (s.find("time") != std::string::npos || s.find("step") != std::string::npos || s.find("alpha") != std::string::npos) timestep_name_ = name;
        } else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
            out_name_ = name;
        }
    }
    if (inCount >= 3) {
        variant_ = Variant::MultiTensor3Input;
        if (img0_name_.empty()) img0_name_ = engine->getIOTensorName(0);
        if (img1_name_.empty()) img1_name_ = engine->getIOTensorName(1);
        if (timestep_name_.empty()) timestep_name_ = engine->getIOTensorName(2);
    } else {
        variant_ = Variant::SingleTensor7Ch;
    }

    const char* check_tensor_name = (variant_ == Variant::SingleTensor7Ch) ? in_name_.c_str() : img0_name_.c_str();
    const auto inType = engine->getTensorDataType(check_tensor_name);
    if (inType != nvinfer1::DataType::kFLOAT && inType != nvinfer1::DataType::kHALF) {
        if (error) *error = "RIFE model input tensor dtype " + dtypeToString(inType) + " unsupported";
        return false;
    }
    in_dtype_ = static_cast<int32_t>(inType);
    if (variant_ == Variant::SingleTensor7Ch) {
        const auto outType = engine->getTensorDataType(out_name_.c_str());
        if (outType != nvinfer1::DataType::kFLOAT && outType != nvinfer1::DataType::kHALF) {
            if (error) *error = "RIFE model output tensor dtype " + dtypeToString(outType) + " unsupported";
            return false;
        }
        out_dtype_ = static_cast<int32_t>(outType);
    } else {
        out_dtype_ = static_cast<int32_t>(nvinfer1::DataType::kFLOAT);
        if (inType != nvinfer1::DataType::kFLOAT) {
            if (error) *error = "MultiTensor3Input RIFE variant requires kFLOAT input, got " + dtypeToString(inType);
            return false;
        }
    }

    const float eff_scale = (scale_ > 0.0f && scale_ <= 1.0f) ? scale_ : 1.0f;
    infer_width_ = static_cast<uint32_t>(std::lround(static_cast<float>(width) * eff_scale));
    infer_height_ = static_cast<uint32_t>(std::lround(static_cast<float>(height) * eff_scale));
    const uint32_t infer_align = (align_mod_ > 0u) ? align_mod_ : 128u;
    const uint32_t aligned_w = ((infer_width_ + infer_align - 1u) / infer_align) * infer_align;
    const uint32_t aligned_h = ((infer_height_ + infer_align - 1u) / infer_align) * infer_align;
    padded_width_ = aligned_w;
    padded_height_ = aligned_h;

    bool shape_resolved = false;
    if (const auto td = engine->getTensorShape(check_tensor_name); td.nbDims == 4 && td.d[2] > 0 && td.d[3] > 0) {
        padded_height_ = static_cast<uint32_t>(td.d[2]);
        padded_width_ = static_cast<uint32_t>(td.d[3]);
        shape_resolved = true;
    }
    if (!shape_resolved && engine->getNbOptimizationProfiles() > 0) {
        if (const int nb_profiles = engine->getNbOptimizationProfiles(); nb_profiles != 1) {
            if (error) *error = "RIFE TensorRT engine must contain exactly 1 optimization profile (found " + std::to_string(nb_profiles) + ")";
            return false;
        }
        const auto fits = [](int64_t lo, int64_t hi, int64_t v) { return v >= lo && v <= hi; };
        const auto min_d = engine->getProfileShape(check_tensor_name, 0, nvinfer1::OptProfileSelector::kMIN);
        const auto max_d = engine->getProfileShape(check_tensor_name, 0, nvinfer1::OptProfileSelector::kMAX);
        if (min_d.nbDims == 4 && max_d.nbDims == 4) {
            const bool h_al = fits(min_d.d[2], max_d.d[2], aligned_h);
            const bool w_al = fits(min_d.d[3], max_d.d[3], aligned_w);
            const bool h_raw = fits(min_d.d[2], max_d.d[2], static_cast<int64_t>(infer_height_));
            const bool w_raw = fits(min_d.d[3], max_d.d[3], static_cast<int64_t>(infer_width_));
            if ((h_al || h_raw) && (w_al || w_raw)) {
                padded_height_ = h_al ? aligned_h : infer_height_;
                padded_width_ = w_al ? aligned_w : infer_width_;
                shape_resolved = true;
            }
        }
        if (!shape_resolved) {
            if (error) *error = "RIFE TensorRT single-profile engine cannot accommodate frame geometry";
            return false;
        }
    }

    const size_t plane_pixels = static_cast<size_t>(padded_width_) * padded_height_;
    const auto in_dt = static_cast<nvinfer1::DataType>(in_dtype_);
    const auto out_dt = static_cast<nvinfer1::DataType>(out_dtype_);

    if (variant_ == Variant::SingleTensor7Ch) {
        input_bytes_ = 7 * plane_pixels * sizeof(float);
        output_bytes_ = 3 * plane_pixels * sizeof(float);
        if (nv_cuda::memAlloc(&d_input_fp32_scratch_, input_bytes_) != 0 ||
            nv_cuda::memAlloc(&d_output_fp32_scratch_, output_bytes_) != 0) {
            if (error) *error = "Failed to allocate RIFE fp32 scratch buffers on GPU";
            return false;
        }
        if (in_dt == nvinfer1::DataType::kHALF) {
            if (nv_cuda::memAlloc(&d_input_tensor_, 7 * plane_pixels * 2u) != 0) {
                if (error) *error = "Failed to allocate 7-channel kHALF input tensor buffer";
                return false;
            }
        } else {
            d_input_tensor_ = d_input_fp32_scratch_;
        }
        if (out_dt == nvinfer1::DataType::kHALF) {
            if (nv_cuda::memAlloc(&d_output_tensor_, 3 * plane_pixels * 2u) != 0) {
                if (error) *error = "Failed to allocate 3-channel kHALF output tensor buffer";
                return false;
            }
        } else {
            d_output_tensor_ = d_output_fp32_scratch_;
        }
        const nvinfer1::Dims4 dims{1, 7, static_cast<int32_t>(padded_height_), static_cast<int32_t>(padded_width_)};
        if (!context->setInputShape(in_name_.c_str(), dims)) {
            if (error) *error = "TensorRT setInputShape failed for 7-channel input";
            return false;
        }
        context->setTensorAddress(in_name_.c_str(), reinterpret_cast<void*>(d_input_tensor_));
        context->setTensorAddress(out_name_.c_str(), reinterpret_cast<void*>(d_output_tensor_));
    } else {
        input_bytes_ = 3 * plane_pixels * sizeof(float);
        output_bytes_ = 3 * plane_pixels * sizeof(float);
        if (nv_cuda::memAlloc(&d_output_tensor_, output_bytes_) != 0 ||
            nv_cuda::memAlloc(&d_img0_tensor_, input_bytes_) != 0 ||
            nv_cuda::memAlloc(&d_img1_tensor_, input_bytes_) != 0) {
            if (error) *error = "Failed to allocate separate RIFE tensor buffers on GPU";
            return false;
        }
        const nvinfer1::Dims4 dims{1, 3, static_cast<int32_t>(padded_height_), static_cast<int32_t>(padded_width_)};
        if (!context->setInputShape(img0_name_.c_str(), dims) || !context->setInputShape(img1_name_.c_str(), dims)) {
            if (error) *error = "TensorRT setInputShape failed for 3-channel input";
            return false;
        }
        context->setTensorAddress(img0_name_.c_str(), reinterpret_cast<void*>(d_img0_tensor_));
        context->setTensorAddress(img1_name_.c_str(), reinterpret_cast<void*>(d_img1_tensor_));
        if (!timestep_name_.empty()) {
            (void)nv_cuda::memAlloc(&d_timestep_tensor_, plane_pixels * sizeof(float));
            const nvinfer1::Dims4 ts_dims{1, 1, static_cast<int32_t>(padded_height_), static_cast<int32_t>(padded_width_)};
            if (!context->setInputShape(timestep_name_.c_str(), ts_dims)) {
                if (error) *error = "TensorRT setInputShape failed for timestep tensor";
                return false;
            }
            context->setTensorAddress(timestep_name_.c_str(), reinterpret_cast<void*>(d_timestep_tensor_));
        }
        context->setTensorAddress(out_name_.c_str(), reinterpret_cast<void*>(d_output_tensor_));
    }

    if (nv_cuda::memAlloc(&d_sad_sum_, sizeof(uint64_t)) != 0 ||
        nv_cuda::memAlloc(&d_block_sad_, 64 * sizeof(uint32_t)) != 0) {
        if (error) *error = "Failed to allocate SAD device buffers";
        return false;
    }

    if (variant_ == Variant::SingleTensor7Ch) {
        constexpr uint32_t kProbeSize = 256u;
        const size_t probe_nv12_bytes = static_cast<size_t>(kProbeSize) * kProbeSize * 3u / 2u;
        const size_t probe_out_bytes = 3 * plane_pixels * sizeof(float);
        unsigned char* host_prev = nullptr;
        unsigned char* host_curr = nullptr;
        float* host_out_a = nullptr;
        float* host_out_b = nullptr;
        NvCuDevicePtr dev_prev = 0, dev_curr = 0;
        std::string probe_error;
        bool ok = false;

        do {
            if (nv_cuda::memAllocHost(reinterpret_cast<void**>(&host_prev), probe_nv12_bytes) != 0 ||
                nv_cuda::memAllocHost(reinterpret_cast<void**>(&host_curr), probe_nv12_bytes) != 0 ||
                nv_cuda::memAllocHost(reinterpret_cast<void**>(&host_out_a), probe_out_bytes) != 0 ||
                nv_cuda::memAllocHost(reinterpret_cast<void**>(&host_out_b), probe_out_bytes) != 0) {
                probe_error = "RIFE alpha-sensitivity probe: cuMemAllocHost failed";
                break;
            }
            std::memset(host_prev, 0x10, static_cast<size_t>(kProbeSize) * kProbeSize);
            std::memset(host_prev + static_cast<size_t>(kProbeSize) * kProbeSize, 0x80, probe_nv12_bytes - static_cast<size_t>(kProbeSize) * kProbeSize);
            std::memset(host_curr, 0xE0, static_cast<size_t>(kProbeSize) * kProbeSize);
            std::memset(host_curr + static_cast<size_t>(kProbeSize) * kProbeSize, 0x80, probe_nv12_bytes - static_cast<size_t>(kProbeSize) * kProbeSize);

            if (nv_cuda::memAlloc(&dev_prev, probe_nv12_bytes) != 0 ||
                nv_cuda::memAlloc(&dev_curr, probe_nv12_bytes) != 0 ||
                nv_cuda::memcpyHtoD(dev_prev, host_prev, probe_nv12_bytes) != 0 ||
                nv_cuda::memcpyHtoD(dev_curr, host_curr, probe_nv12_bytes) != 0 ||
                nv_cuda::memsetD32Async(d_sad_sum_, 0, 2, nullptr) != 0) {
                probe_error = "RIFE alpha-sensitivity probe: GPU buffer setup failed";
                break;
            }
            if (launchRifePreprocess(
                    std::make_tuple(static_cast<uint64_t>(dev_prev), uint64_t{0}, static_cast<uint64_t>(dev_curr), uint64_t{0},
                                    d_input_fp32_scratch_, kProbeSize, kProbeSize, kProbeSize, kProbeSize,
                                    padded_width_, padded_height_, 0.2f, int32_t{0}, d_sad_sum_, int32_t{1}, uint32_t{0}, uint32_t{0}),
                    nullptr) != RTX_OK) {
                probe_error = "RIFE alpha-sensitivity probe: preprocess kernel launch failed";
                break;
            }
            if (in_dt == nvinfer1::DataType::kHALF &&
                launchFp32ToFp16(d_input_fp32_scratch_, d_input_tensor_, static_cast<uint32_t>(7 * plane_pixels), nullptr) != RTX_OK) {
                probe_error = "RIFE alpha-sensitivity probe: fp32->fp16 input conversion failed";
                break;
            }
            (void)nv_cuda::streamSync(nullptr);

            float* out_targets[2] = {host_out_a, host_out_b};
            const float alphas[2] = {0.2f, 0.8f};
            bool infer_ok = true;
            for (int i = 0; i < 2 && infer_ok; ++i) {
                const uint64_t ch6_addr = d_input_fp32_scratch_ + 6 * plane_pixels * sizeof(float);
                uint32_t alpha_bits = 0;
                std::memcpy(&alpha_bits, &alphas[i], sizeof(alpha_bits));
                if (nv_cuda::memsetD32Async(ch6_addr, alpha_bits, plane_pixels, nullptr) != 0 ||
                    (in_dt == nvinfer1::DataType::kHALF &&
                     launchFp32ToFp16(ch6_addr, d_input_tensor_ + 6 * plane_pixels * sizeof(uint16_t),
                                      static_cast<uint32_t>(plane_pixels), nullptr) != RTX_OK) ||
                    !context->enqueueV3(nullptr) || nv_cuda::streamSync(nullptr) != 0 ||
                    (out_dt == nvinfer1::DataType::kHALF &&
                     launchFp16ToFp32(d_output_tensor_, d_output_fp32_scratch_,
                                      static_cast<uint32_t>(3 * plane_pixels), nullptr) != RTX_OK) ||
                    nv_cuda::memcpyDtoH(out_targets[i], d_output_fp32_scratch_, probe_out_bytes) != 0) {
                    probe_error = "RIFE alpha-sensitivity probe: inference/readback failed";
                    infer_ok = false;
                }
            }
            if (!infer_ok) break;
            double l1 = 0.0;
            for (size_t i = 0; i < 3 * plane_pixels; ++i) {
                l1 += std::fabs(static_cast<double>(host_out_a[i]) - static_cast<double>(host_out_b[i]));
            }
            if (l1 == 0.0) {
                probe_error = "engine ignores timestep channel (alpha): outputs identical at alpha 0.2 vs 0.8";
                break;
            }
            alpha_probe_passed_ = true;
            ok = true;
        } while (false);

        if (dev_prev) (void)nv_cuda::memFree(dev_prev);
        if (dev_curr) (void)nv_cuda::memFree(dev_curr);
        if (host_prev) (void)nv_cuda::memFreeHost(host_prev);
        if (host_curr) (void)nv_cuda::memFreeHost(host_curr);
        if (host_out_a) (void)nv_cuda::memFreeHost(host_out_a);
        if (host_out_b) (void)nv_cuda::memFreeHost(host_out_b);
        resetFramePairCache();

        if (!ok) {
            if (error) *error = probe_error;
            releaseBuffers();
            releaseTrtObjects();
            return false;
        }
    }

    const uint64_t trt_workspace = static_cast<uint64_t>(engine->getDeviceMemorySizeV2());
    trt_peak_allocation_bytes_ = trt_workspace + static_cast<uint64_t>(input_bytes_ + output_bytes_);
    if (out_trt_peak_bytes) *out_trt_peak_bytes = trt_peak_allocation_bytes_;
    return true;
}

int32_t RifeEngine::interpolate(
    uint64_t prev_nv12, uint64_t curr_nv12, uint32_t pitch, float alpha,
    uint64_t out_nv12, int32_t full_range, int32_t* scene_cut, void* stream) {
    if (!context_ || prev_nv12 == 0 || curr_nv12 == 0 || out_nv12 == 0 || pitch == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    ++interpolate_calls_;
    if (scene_cut) *scene_cut = 0;

    const size_t total_nv12_bytes = static_cast<size_t>(pitch) * height_ * 3u / 2u;
    const size_t plane_pixels = static_cast<size_t>(padded_width_) * padded_height_;
    const auto in_dt = static_cast<nvinfer1::DataType>(in_dtype_);
    const auto out_dt = static_cast<nvinfer1::DataType>(out_dtype_);

    bool reuse = false;
    int32_t effective_scene_cut = 0;
    if (variant_ == Variant::SingleTensor7Ch && cache_valid_ &&
        prev_nv12 == last_prev_nv12_ && curr_nv12 == last_curr_nv12_ &&
        pitch == last_pitch_ && d_input_fp32_scratch_ != 0) {
        const uint64_t fp32_ch6 = d_input_fp32_scratch_ + 6ULL * plane_pixels * sizeof(float);
        uint32_t alpha_bits = 0;
        std::memcpy(&alpha_bits, &alpha, sizeof(alpha_bits));
        bool queued = false;
        if (in_dt == nvinfer1::DataType::kHALF && d_input_tensor_ != 0) {
            const uint64_t fp16_ch6 = d_input_tensor_ + 6ULL * plane_pixels * sizeof(uint16_t);
            if (nv_cuda::memsetD32Async(fp32_ch6, alpha_bits, plane_pixels, stream) == 0) {
                queued = (launchFp32ToFp16(fp32_ch6, fp16_ch6, static_cast<uint32_t>(plane_pixels), stream) == RTX_OK);
            }
        } else {
            queued = (nv_cuda::memsetD32Async(fp32_ch6, alpha_bits, plane_pixels, stream) == 0);
        }
        if (queued) {
            ++cache_reuse_;
            reuse = true;
            effective_scene_cut = last_scene_cut_;
        }
    }
    if (!reuse) {
        ++preprocess_launches_;
        RtxResult prep_res = RTX_OK;
        if (d_sad_sum_) (void)nv_cuda::memsetD32Async(d_sad_sum_, 0, 2, stream);
        if (variant_ == Variant::SingleTensor7Ch) {
            prep_res = launchRifePreprocess(
                std::make_tuple(prev_nv12, uint64_t{0}, curr_nv12, uint64_t{0}, d_input_fp32_scratch_,
                                width_, height_, pitch, pitch, padded_width_, padded_height_,
                                alpha, full_range, d_sad_sum_, reflect_padding_ ? 1 : 0, infer_width_, infer_height_),
                stream);
            if (prep_res == RTX_OK && in_dt == nvinfer1::DataType::kHALF) {
                prep_res = launchFp32ToFp16(d_input_fp32_scratch_, d_input_tensor_,
                                            static_cast<uint32_t>(7 * plane_pixels), stream);
            }
        } else {
            prep_res = launchRifePreprocessSeparate(
                std::make_tuple(prev_nv12, prev_nv12 + static_cast<uint64_t>(pitch) * height_,
                                curr_nv12, curr_nv12 + static_cast<uint64_t>(pitch) * height_,
                                d_img0_tensor_, d_img1_tensor_, d_timestep_tensor_,
                                width_, height_, pitch, pitch, padded_width_, padded_height_,
                                alpha, full_range, d_sad_sum_, int32_t{1}),
                stream);
        }
        if (prep_res != RTX_OK) return prep_res;

        if (d_block_sad_) {
            (void)nv_cuda::memsetD32Async(d_block_sad_, 0, 64, stream);
            (void)launchEvalBlockSad8x8(prev_nv12, curr_nv12, pitch, width_, height_, d_block_sad_, stream);
            (void)nv_cuda::streamSync(stream);
            std::array<uint32_t, 64> block_sad{};
            (void)nv_cuda::memcpyDtoH(block_sad.data(), d_block_sad_, sizeof(block_sad));
            const bool is_cut = evalBlockSadSceneCut(block_sad, width_, height_, ema_motion_);
            effective_scene_cut = (is_cut || (scene_cut && *scene_cut != 0)) ? 1 : 0;
        } else if (d_sad_sum_) {
            (void)nv_cuda::streamSync(stream);
            uint64_t host_sad = 0;
            (void)nv_cuda::memcpyDtoH(&host_sad, d_sad_sum_, sizeof(uint64_t));
            const double total_pixels = static_cast<double>(width_) * static_cast<double>(height_);
            const float diff_norm = (total_pixels > 0.0)
                                        ? static_cast<float>(static_cast<double>(host_sad) / (total_pixels * 255.0))
                                        : 0.0f;
            effective_scene_cut = (diff_norm > 0.35f) ? 1 : 0;
        }
        if (variant_ == Variant::SingleTensor7Ch) {
            last_prev_nv12_ = prev_nv12;
            last_curr_nv12_ = curr_nv12;
            last_pitch_ = pitch;
            last_scene_cut_ = effective_scene_cut;
            cache_valid_ = true;
        }
    }
    if (scene_cut) *scene_cut = effective_scene_cut;
    if (effective_scene_cut != 0) {
        const uint64_t src = (alpha < 0.5f) ? prev_nv12 : curr_nv12;
        (void)nv_cuda::memcpyDtoDAsync(out_nv12, src, total_nv12_bytes, stream);
        return RTX_OK;
    }
    if (!static_cast<nvinfer1::IExecutionContext*>(context_)->enqueueV3(reinterpret_cast<cudaStream_t>(stream))) {
        const uint64_t src = (alpha < 0.5f) ? prev_nv12 : curr_nv12;
        (void)nv_cuda::memcpyDtoDAsync(out_nv12, src, total_nv12_bytes, stream);
        return RTX_ERR_INTERNAL;
    }
    if (out_dt == nvinfer1::DataType::kHALF) {
        const size_t out_elems = 3ULL * static_cast<size_t>(padded_width_) * static_cast<size_t>(padded_height_);
        if (const int32_t cvt_res = launchFp16ToFp32(d_output_tensor_, d_output_fp32_scratch_,
                                                     static_cast<uint32_t>(out_elems), stream);
            cvt_res != RTX_OK) {
            return cvt_res;
        }
    }
    return launchRifePostprocess(
        std::make_tuple(d_output_fp32_scratch_, out_nv12, uint64_t{0},
                        width_, height_, pitch, pitch, padded_width_, padded_height_,
                        full_range, infer_width_, infer_height_),
        stream);
}

}  // namespace rtx_ngx
