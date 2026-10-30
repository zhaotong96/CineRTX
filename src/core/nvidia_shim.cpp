// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/nvidia_shim.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#if defined(RTX_EMBEDDED_SHIM)
#include "rtx/embedded_shim.h"
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace rtx {
namespace {

#if defined(_WIN32)
std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                        static_cast<int>(value.size()), wide.data(), size);
    return wide;
}

std::string executableDirectory() {
    wchar_t buffer[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length == 0 || length >= 32768) {
        return {};
    }
    const std::filesystem::path path(std::wstring(buffer, length));
    const std::u8string directory = path.parent_path().u8string();
    return std::string(directory.begin(), directory.end());
}
#else
std::string executableDirectory() {
    char buffer[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    return std::filesystem::path(std::string(buffer, static_cast<size_t>(length)))
        .parent_path()
        .string();
}
#endif

std::string shimFileName() {
#if defined(_WIN32)
    return "rtx_shim.dll";
#elif defined(__APPLE__)
    return "librtx_shim.dylib";
#else
    return "librtx_shim.so";
#endif
}

std::vector<std::string> candidatePaths() {
    std::vector<std::string> paths;
    const char* configured = std::getenv("RTX_SHIM_PATH");
    if (configured != nullptr && configured[0] != '\0') {
        paths.emplace_back(configured);
    }
    const std::string directory = executableDirectory();
    if (!directory.empty()) {
        paths.push_back((std::filesystem::path(directory) / shimFileName()).string());
    }
    return paths;
}

}  // namespace

NvidiaShim::~NvidiaShim() { close(); }

template <typename Fn>
bool NvidiaShim::resolve(Fn& function, const char* name, std::string* error) {
#if defined(_WIN32)
    function = reinterpret_cast<Fn>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
#else
    function = reinterpret_cast<Fn>(dlsym(handle_, name));
#endif
    if (function == nullptr) {
        if (error != nullptr) {
            *error = std::string("rtx_shim is missing the symbol ") + name;
        }
        return false;
    }
    return true;
}

#if defined(RTX_EMBEDDED_SHIM)
bool NvidiaShim::loadEmbedded(std::string* error) {
    const EmbeddedShimApi* api = embeddedShimApi();
    if (api == nullptr || api->abi_version == nullptr) {
        return false;
    }
    if (api->abi_version() != RTX_SHIM_ABI_VERSION) {
        error_ = "embedded rtx_shim ABI version " + std::to_string(api->abi_version()) +
                 " does not match the engine expectation " +
                 std::to_string(RTX_SHIM_ABI_VERSION);
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }
    probe_ = api->probe;
    decoder_create_ = api->decoder_create;
    decoder_destroy_ = api->decoder_destroy;
    decoder_submit_ = api->decoder_submit;
    decoder_receive_ = api->decoder_receive;
    decoder_flush_ = api->decoder_flush;
    decoder_release_ = api->decoder_release_frame;
    decoder_download_ = api->decoder_download;
    decoder_stats_ = api->decoder_get_stats;
    decoder_last_error_ = api->decoder_last_error;
    encoder_create_ = api->encoder_create;
    encoder_destroy_ = api->encoder_destroy;
    encoder_encode_ = api->encoder_encode;
    encoder_encode_host_ = api->encoder_encode_host;
    encoder_flush_ = api->encoder_flush;
    encoder_sequence_header_ = api->encoder_get_sequence_header;
    encoder_stats_ = api->encoder_get_stats;
    encoder_last_error_ = api->encoder_last_error;
    embedded_ = true;
    path_ = "embedded:rtx_shim";
    error_.clear();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}
#endif

bool NvidiaShim::open(std::string* error) {
    if (loaded()) {
        return true;
    }
    close();
    const std::vector<std::string> candidates = candidatePaths();
    if (candidates.empty()) {
#if defined(RTX_EMBEDDED_SHIM)
        if (loadEmbedded(error)) {
            return true;
        }
#endif
        error_ = "cannot determine the executable directory; set RTX_SHIM_PATH";
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }

    std::string last_error;
    for (const std::string& candidate : candidates) {
#if defined(_WIN32)
        const std::wstring wide = widen(candidate);
        if (wide.empty()) {
            last_error = "invalid UTF-8 shim path: " + candidate;
            continue;
        }
        HMODULE module = LoadLibraryExW(
            wide.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr) {
            last_error = "LoadLibraryExW failed for " + candidate + " (error " +
                         std::to_string(GetLastError()) + ")";
            continue;
        }
        handle_ = reinterpret_cast<void*>(module);
#else
        handle_ = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle_ == nullptr) {
            const char* message = dlerror();
            last_error = candidate + ": " + (message != nullptr ? message : "dlopen failed");
            continue;
        }
#endif
        path_ = candidate;
        break;
    }
    if (handle_ == nullptr) {
        // The single-EXE build links rtx_shim_static, so a missing sidecar is
        // not a failure: the exact same codec implementation is in the image.
#if defined(RTX_EMBEDDED_SHIM)
        if (loadEmbedded(error)) {
            return true;
        }
#endif
        error_ = last_error.empty() ? "rtx_shim not found" : last_error;
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }

    std::string message;
    uint32_t(RTX_SHIM_CALL * abi_version)() = nullptr;
    if (!resolve(abi_version, "rtx_shim_abi_version", &message)) {
        close();
#if defined(RTX_EMBEDDED_SHIM)
        if (loadEmbedded(error)) {
            return true;
        }
#endif
        error_ = message;
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }
    if (abi_version() != RTX_SHIM_ABI_VERSION) {
        const uint32_t actual = abi_version();
        close();
#if defined(RTX_EMBEDDED_SHIM)
        if (loadEmbedded(error)) {
            return true;
        }
#endif
        error_ = "rtx_shim ABI version " + std::to_string(actual) +
                 " does not match the engine expectation " +
                 std::to_string(RTX_SHIM_ABI_VERSION);
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }

    const bool complete =
        resolve(probe_, "rtx_shim_probe", &message) &&
        resolve(decoder_create_, "rtx_shim_decoder_create", &message) &&
        resolve(decoder_destroy_, "rtx_shim_decoder_destroy", &message) &&
        resolve(decoder_submit_, "rtx_shim_decoder_submit", &message) &&
        resolve(decoder_receive_, "rtx_shim_decoder_receive", &message) &&
        resolve(decoder_flush_, "rtx_shim_decoder_flush", &message) &&
        resolve(decoder_release_, "rtx_shim_decoder_release_frame", &message) &&
        resolve(decoder_download_, "rtx_shim_decoder_download", &message) &&
        resolve(decoder_stats_, "rtx_shim_decoder_get_stats", &message) &&
        resolve(decoder_last_error_, "rtx_shim_decoder_last_error", &message) &&
        resolve(encoder_create_, "rtx_shim_encoder_create", &message) &&
        resolve(encoder_destroy_, "rtx_shim_encoder_destroy", &message) &&
        resolve(encoder_encode_, "rtx_shim_encoder_encode", &message) &&
        resolve(encoder_encode_host_, "rtx_shim_encoder_encode_host", &message) &&
        resolve(encoder_flush_, "rtx_shim_encoder_flush", &message) &&
        resolve(encoder_sequence_header_, "rtx_shim_encoder_get_sequence_header", &message) &&
        resolve(encoder_stats_, "rtx_shim_encoder_get_stats", &message) &&
        resolve(encoder_last_error_, "rtx_shim_encoder_last_error", &message);
    if (!complete) {
        close();
#if defined(RTX_EMBEDDED_SHIM)
        if (loadEmbedded(error)) {
            return true;
        }
#endif
        error_ = message;
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }

    error_.clear();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void NvidiaShim::close() {
    if (handle_ != nullptr) {
#if defined(_WIN32)
        FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
    embedded_ = false;
    path_.clear();
    probe_ = nullptr;
    decoder_create_ = nullptr;
    decoder_destroy_ = nullptr;
    decoder_submit_ = nullptr;
    decoder_receive_ = nullptr;
    decoder_flush_ = nullptr;
    decoder_release_ = nullptr;
    decoder_download_ = nullptr;
    decoder_stats_ = nullptr;
    decoder_last_error_ = nullptr;
    encoder_create_ = nullptr;
    encoder_destroy_ = nullptr;
    encoder_encode_ = nullptr;
    encoder_encode_host_ = nullptr;
    encoder_flush_ = nullptr;
    encoder_sequence_header_ = nullptr;
    encoder_stats_ = nullptr;
    encoder_last_error_ = nullptr;
}

RtxResult NvidiaShim::probe(RtxDiagnostics* diagnostics) const {
    return probe_ != nullptr ? probe_(diagnostics) : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::decoderCreate(const RtxDecoderConfig* config,
                                    RtxDecoder** decoder) const {
    return decoder_create_ != nullptr ? decoder_create_(config, decoder) : RTX_ERR_DLL_MISSING;
}

void NvidiaShim::decoderDestroy(RtxDecoder* decoder) const {
    if (decoder_destroy_ != nullptr) {
        decoder_destroy_(decoder);
    }
}

RtxResult NvidiaShim::decoderSubmit(RtxDecoder* decoder, const uint8_t* data, size_t size,
                                    int64_t pts, int64_t dts, int32_t flags) const {
    return decoder_submit_ != nullptr ? decoder_submit_(decoder, data, size, pts, dts, flags)
                                      : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::decoderReceive(RtxDecoder* decoder, RtxFrameDesc* frame) const {
    return decoder_receive_ != nullptr ? decoder_receive_(decoder, frame) : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::decoderFlush(RtxDecoder* decoder) const {
    return decoder_flush_ != nullptr ? decoder_flush_(decoder) : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::decoderReleaseFrame(RtxDecoder* decoder, uint64_t slot_index) const {
    return decoder_release_ != nullptr ? decoder_release_(decoder, slot_index)
                                       : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::decoderDownload(RtxDecoder* decoder, const RtxFrameDesc* frame,
                                      uint8_t* destination, size_t destination_pitch) const {
    return decoder_download_ != nullptr
               ? decoder_download_(decoder, frame, destination, destination_pitch)
               : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::decoderStats(RtxDecoder* decoder, RtxHwStats* stats) const {
    return decoder_stats_ != nullptr ? decoder_stats_(decoder, stats) : RTX_ERR_DLL_MISSING;
}

const char* NvidiaShim::decoderLastError(RtxDecoder* decoder) const {
    return decoder_last_error_ != nullptr ? decoder_last_error_(decoder)
                                          : "rtx_shim decoder ABI unavailable";
}

RtxResult NvidiaShim::encoderCreate(const RtxEncoderConfig* config,
                                    RtxEncoder** encoder) const {
    return encoder_create_ != nullptr ? encoder_create_(config, encoder) : RTX_ERR_DLL_MISSING;
}

void NvidiaShim::encoderDestroy(RtxEncoder* encoder) const {
    if (encoder_destroy_ != nullptr) {
        encoder_destroy_(encoder);
    }
}

RtxResult NvidiaShim::encoderEncode(RtxEncoder* encoder, const RtxFrameDesc* frame,
                                    RtxPacketDesc* packet) const {
    return encoder_encode_ != nullptr ? encoder_encode_(encoder, frame, packet)
                                      : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::encoderEncodeHost(RtxEncoder* encoder, const uint8_t* data, uint32_t pitch,
                                        int32_t format, int64_t pts,
                                        RtxPacketDesc* packet) const {
    return encoder_encode_host_ != nullptr
               ? encoder_encode_host_(encoder, data, pitch, format, pts, packet)
               : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::encoderFlush(RtxEncoder* encoder, RtxPacketDesc* packet) const {
    return encoder_flush_ != nullptr ? encoder_flush_(encoder, packet) : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::encoderSequenceHeader(RtxEncoder* encoder, uint8_t* buffer, size_t capacity,
                                            size_t* size) const {
    return encoder_sequence_header_ != nullptr
               ? encoder_sequence_header_(encoder, buffer, capacity, size)
               : RTX_ERR_DLL_MISSING;
}

RtxResult NvidiaShim::encoderStats(RtxEncoder* encoder, RtxHwStats* stats) const {
    return encoder_stats_ != nullptr ? encoder_stats_(encoder, stats) : RTX_ERR_DLL_MISSING;
}

const char* NvidiaShim::encoderLastError(RtxEncoder* encoder) const {
    return encoder_last_error_ != nullptr ? encoder_last_error_(encoder)
                                          : "rtx_shim encoder ABI unavailable";
}

}  // namespace rtx
