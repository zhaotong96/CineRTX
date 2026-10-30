/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT RIFE v4.25 engine construction (W8-1).
 *
 * Zone A/B clean implementation:
 * This translation unit NEVER includes <NvInfer.h> or <NvOnnxParser.h>.
 * In-process TensorRT construction is delegated to the Zone C isolated
 * dynamic bridge rtx_ngx_bridge.dll via the pure C ABI rtx_ngx_build_rife_engine.
 */

#include "rife_engine.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "shim/ngx_bridge_abi.h"

namespace rtx {
namespace {

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4).  ~60 lines beats a build-time dependency on OpenSSL for
// a single-EXE product, and the digest is only used for integrity bookkeeping.
// ---------------------------------------------------------------------------
constexpr uint32_t kSha256RoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotateRight(uint32_t value, uint32_t count) noexcept {
    return (value >> count) | (value << (32u - count));
}

class Sha256 {
public:
    void update(const uint8_t* data, size_t size) {
        if (data == nullptr || size == 0) return;
        total_bytes_ += size;
        while (size > 0) {
            const size_t take = std::min(size, sizeof(buffer_) - buffer_size_);
            std::memcpy(buffer_.data() + buffer_size_, data, take);
            buffer_size_ += take;
            data += take;
            size -= take;
            if (buffer_size_ == sizeof(buffer_)) {
                transform(buffer_.data());
                buffer_size_ = 0;
            }
        }
    }

    std::string hexDigest() {
        uint8_t final_block[128] = {};
        std::memcpy(final_block, buffer_.data(), buffer_size_);
        final_block[buffer_size_] = 0x80u;
        const size_t target_size = buffer_size_ < 56 ? 64 : 128;
        const uint64_t total_bits = total_bytes_ * 8u;
        for (size_t index = 0; index < 8; ++index) {
            final_block[target_size - 1 - index] =
                static_cast<uint8_t>((total_bits >> (index * 8u)) & 0xFFu);
        }
        transform(final_block);
        if (target_size == 128) {
            transform(final_block + 64);
        }
        char hex[65] = {};
        for (size_t word = 0; word < 8; ++word) {
            std::snprintf(hex + word * 8, 9, "%08x", state_[word]);
        }
        return std::string(hex, 64);
    }

private:
    void transform(const uint8_t* block) {
        uint32_t schedule[64] = {};
        for (size_t index = 0; index < 16; ++index) {
            schedule[index] = (static_cast<uint32_t>(block[index * 4]) << 24) |
                              (static_cast<uint32_t>(block[index * 4 + 1]) << 16) |
                              (static_cast<uint32_t>(block[index * 4 + 2]) << 8) |
                              static_cast<uint32_t>(block[index * 4 + 3]);
        }
        for (size_t index = 16; index < 64; ++index) {
            const uint32_t s0 = rotateRight(schedule[index - 15], 7) ^
                                rotateRight(schedule[index - 15], 18) ^
                                (schedule[index - 15] >> 3);
            const uint32_t s1 = rotateRight(schedule[index - 2], 17) ^
                                rotateRight(schedule[index - 2], 19) ^
                                (schedule[index - 2] >> 10);
            schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
        }
        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];
        for (size_t index = 0; index < 64; ++index) {
            const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choose + kSha256RoundConstants[index] + schedule[index];
            const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<uint8_t, 64> buffer_{};
    size_t buffer_size_ = 0;
    uint64_t total_bytes_ = 0;
};

#if defined(_WIN32)
std::wstring executableDirectoryW() {
    wchar_t path[32768] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, path, 32768);
    if (length == 0 || length >= 32768) {
        return {};
    }
    std::wstring directory(path, length);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        directory.resize(slash);
    }
    return directory;
}

HMODULE loadNgxBridgeDll() {
    const std::wstring exe_dir = executableDirectoryW();
    if (exe_dir.empty()) {
        return nullptr;
    }
    // Flat bin layout: candidate 1 is beside CineRTX.exe
    std::wstring flat_path = exe_dir + L"\\rtx_ngx_bridge.dll";
    HMODULE bridge = ::LoadLibraryExW(
        flat_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (bridge != nullptr) {
        return bridge;
    }
    // Backward compatibility: candidate 2 is inside plugins/nvidia_rtx
    std::wstring plugin_path = exe_dir + L"\\plugins\\nvidia_rtx\\rtx_ngx_bridge.dll";
    return ::LoadLibraryExW(
        plugin_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}
#else
void* loadNgxBridgeDll() {
    return dlopen("librtx_ngx_bridge.so", RTLD_NOW | RTLD_LOCAL);
}
#endif

}  // namespace

bool tensorRtBuilderAvailable() noexcept {
#if defined(_WIN32)
    HMODULE bridge = loadNgxBridgeDll();
    if (bridge != nullptr) {
        auto* fn = reinterpret_cast<RtxNgxBuildRifeEngineFn>(
            ::GetProcAddress(bridge, "rtx_ngx_build_rife_engine"));
        ::FreeLibrary(bridge);
        return fn != nullptr;
    }
#endif
    return false;
}

Result<std::string> computeFileSha256(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return Result<std::string>::fail(ErrorCode::Io, "cannot open file for hashing", path);
    }
    Sha256 hasher;
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = stream.gcount();
        if (read > 0) {
            hasher.update(reinterpret_cast<const uint8_t*>(buffer.data()),
                          static_cast<size_t>(read));
        }
    }
    if (!stream.eof()) {
        return Result<std::string>::fail(ErrorCode::Io, "cannot read file for hashing", path);
    }
    return Result<std::string>::ok(hasher.hexDigest());
}

std::string engineSidecarPath(const std::string& engine_path) {
    return engine_path + ".sha256";
}

Status writeEngineSidecar(const std::string& engine_path, const std::string& onnx_sha256) {
    const std::string sidecar = engineSidecarPath(engine_path);
    const std::string temporary = sidecar + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return Status::fail(ErrorCode::Io, "cannot create the engine sidecar", temporary);
        }
        stream << onnx_sha256 << "\n";
        if (!stream) {
            return Status::fail(ErrorCode::Io, "cannot write the engine sidecar", temporary);
        }
    }
    std::remove(sidecar.c_str());
    if (std::rename(temporary.c_str(), sidecar.c_str()) != 0) {
        std::remove(temporary.c_str());
        return Status::fail(ErrorCode::Io, "cannot publish the engine sidecar", sidecar);
    }
    return Status::success();
}

Result<std::string> readEngineSidecar(const std::string& engine_path) {
    const std::string sidecar = engineSidecarPath(engine_path);
    std::ifstream stream(sidecar, std::ios::binary);
    if (!stream) {
        return Result<std::string>::fail(ErrorCode::NotFound,
                                         "engine integrity sidecar is missing", sidecar);
    }
    std::string digest;
    std::getline(stream, digest);
    while (!digest.empty() && (digest.back() == '\r' || digest.back() == '\n' ||
                               digest.back() == ' ')) {
        digest.pop_back();
    }
    if (digest.size() != 64) {
        return Result<std::string>::fail(ErrorCode::InvalidArgument,
                                         "engine sidecar does not hold a SHA-256 digest",
                                         sidecar);
    }
    return Result<std::string>::ok(digest);
}

Result<RifeEngineBuildReport> buildRifeEngine(const RifeEngineBuildRequest& request) {
    if (request.onnx_path.empty() || request.engine_path.empty()) {
        return Result<RifeEngineBuildReport>::fail(
            ErrorCode::InvalidArgument, "engine build requires an ONNX input and an engine output");
    }

    RifeConfig config;
    config.source_width = request.width;
    config.source_height = request.height;
    const RifeTensorShape shape = rifeAlignedShape(request.width, request.height);
    config.tensor_width = shape.w;
    config.tensor_height = shape.h;
    config.fp16 = request.fp16;
    const Result<void> valid = validateRifeConfig(config);
    if (!valid.ok()) {
        return Result<RifeEngineBuildReport>::fromStatus(valid.status());
    }

    const Result<void> version = validateTensorRtVersion(kRifeTensorRtBaseline);
    if (!version.ok()) {
        return Result<RifeEngineBuildReport>::fromStatus(version.status());
    }

    const Result<std::string> digest = computeFileSha256(request.onnx_path);
    if (!digest.ok()) {
        return Result<RifeEngineBuildReport>::fail(digest.error());
    }

    RifeEngineBuildReport report;
    report.onnx_sha256 = digest.value();
    report.sidecar_path = engineSidecarPath(request.engine_path);
    report.tensorrt_version = kRifeTensorRtBaseline;
    report.trtexec_command =
        buildTrtexecCommand(config, request.onnx_path, request.engine_path);

#if defined(_WIN32)
    HMODULE bridge = loadNgxBridgeDll();
    if (bridge != nullptr) {
        auto* fn = reinterpret_cast<RtxNgxBuildRifeEngineFn>(
            ::GetProcAddress(bridge, "rtx_ngx_build_rife_engine"));
        if (fn != nullptr) {
            RtxNgxRifeBuildParams params{};
            params.onnx_path = request.onnx_path.c_str();
            params.engine_path = request.engine_path.c_str();
            params.width = request.width;
            params.height = request.height;
            params.fp16 = request.fp16 ? 1 : 0;
            params.verbose = request.verbose ? 1 : 0;

            RtxNgxRifeBuildResult result{};
            const RtxResult ret = fn(&params, &result);
            if (ret == RTX_OK) {
                if (result.built_in_process != 0) {
                    report.built_in_process = true;
                    report.engine_bytes = result.engine_bytes;
                    if (result.tensorrt_version[0] != '\0') {
                        report.tensorrt_version = result.tensorrt_version;
                    }
                } else if (result.error_message[0] != '\0') {
                    report.runtime_note = result.error_message;
                }
            }
        }
        ::FreeLibrary(bridge);
    }
#endif

    const Status sidecar = writeEngineSidecar(request.engine_path, report.onnx_sha256);
    if (!sidecar.ok()) {
        return Result<RifeEngineBuildReport>::fromStatus(sidecar);
    }
    return Result<RifeEngineBuildReport>::ok(std::move(report));
}

}  // namespace rtx
