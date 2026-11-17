// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Implementation of the rtx_ngx_bridge runtime loader (see
// include/rtx/ngx_remaster.h).  Mirrors nvidia_shim.cpp: candidate paths
// (explicit override, <exe>/plugins/nvidia_rtx, then the executable
// directory), LoadLibraryExW with restricted search, symbol resolution and an
// explicit ABI version gate before the remaster contract may be used.  A
// sidecar DLL is honoured first for plugin override; when none is present the
// single-EXE build falls back to bindEmbedded(), which binds the identical C
// ABI that rtx_ngx_bridge_static linked into this image.

#include "rtx/ngx_remaster.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

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

std::string bridgeFileName() {
#if defined(_WIN32)
    return "rtx_ngx_bridge.dll";
#elif defined(__APPLE__)
    return "librtx_ngx_bridge.dylib";
#else
    return "librtx_ngx_bridge.so";
#endif
}

}  // namespace

NgxRemasterBridge::~NgxRemasterBridge() { close(); }

#if defined(RTX_EMBEDDED_NGX_BRIDGE)
bool NgxRemasterBridge::bindEmbedded(std::string* error) {
    if (rtx_ngx_bridge_abi_version() < RTX_NGX_BRIDGE_ABI_VERSION) {
        error_ = "embedded rtx_ngx_bridge ABI version " +
                 std::to_string(rtx_ngx_bridge_abi_version()) +
                 " is older than the remaster contract " +
                 std::to_string(RTX_NGX_BRIDGE_ABI_VERSION);
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }
    query_caps_ = &rtx_ngx_bridge_query_caps;
    create_ = &rtx_ngx_bridge_create;
    destroy_ = &rtx_ngx_bridge_destroy;
    remaster_ = &rtx_ngx_bridge_remaster;
    get_remaster_stats_ = &rtx_ngx_bridge_get_remaster_stats;
    last_error_ = &rtx_ngx_bridge_last_error;
    set_lut3d_ = &rtx_ngx_bridge_set_lut3d;
    preview_buffer_ = &rtx_ngx_bridge_preview_buffer;
    download_preview_ = &rtx_ngx_bridge_download_preview;
    upload_frame_ = &rtx_ngx_bridge_upload_frame;
    interpolate_nv12_ = &rtx_ngx_bridge_interpolate_nv12;
    get_interp_buffer_ = &rtx_ngx_bridge_get_interp_buffer;
    init_rife_ = &rtx_ngx_bridge_init_rife;
    interpolate_rife_ = &rtx_ngx_bridge_interpolate_rife;
    build_rife_engine_ = &rtx_ngx_bridge_build_rife_engine;
    destroy_rife_ = &rtx_ngx_bridge_destroy_rife;
    embedded_ = true;
    path_ = "embedded:rtx_ngx_bridge";
    error_.clear();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}
#endif

bool NgxRemasterBridge::open(std::string* error) {
    if (loaded()) {
        return true;
    }
    close();
#if defined(RTX_EMBEDDED_NGX_BRIDGE)
    return bindEmbedded(error);
#else
    if (error != nullptr) {
        *error = "rtx_ngx_bridge dynamic loading is deprecated; build with RTX_EMBEDDED_NGX_BRIDGE=1";
    }
    return false;
#endif
}

void NgxRemasterBridge::close() {
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
    query_caps_ = nullptr;
    create_ = nullptr;
    destroy_ = nullptr;
    remaster_ = nullptr;
    get_remaster_stats_ = nullptr;
    last_error_ = nullptr;
    set_lut3d_ = nullptr;
    preview_buffer_ = nullptr;
    download_preview_ = nullptr;
    upload_frame_ = nullptr;
    interpolate_nv12_ = nullptr;
    get_interp_buffer_ = nullptr;
    init_rife_ = nullptr;
    interpolate_rife_ = nullptr;
    build_rife_engine_ = nullptr;
    destroy_rife_ = nullptr;
}

RtxResult NgxRemasterBridge::queryCaps(RtxNgxBridgeCaps* caps) const {
    return query_caps_ != nullptr ? query_caps_(caps) : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::create(const RtxNgxBridgeConfig* config,
                                    RtxNgxBridge** bridge) const {
    return create_ != nullptr ? create_(config, bridge) : RTX_ERR_DLL_MISSING;
}

void NgxRemasterBridge::releaseBridge(RtxNgxBridge* bridge) const {
    if (destroy_ != nullptr) {
        destroy_(bridge);
    }
}

RtxResult NgxRemasterBridge::remaster(RtxNgxBridge* bridge, const RtxNgxRemasterInput* input,
                                      RtxNgxRemasterOutput* output,
                                      RtxNgxRemasterStats* stats) const {
    return remaster_ != nullptr ? remaster_(bridge, input, output, stats) : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::getRemasterStats(RtxNgxBridge* bridge,
                                              RtxNgxRemasterStats* stats) const {
    return get_remaster_stats_ != nullptr ? get_remaster_stats_(bridge, stats)
                                          : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::setLut3d(RtxNgxBridge* bridge, const uint16_t* lut_half4_data,
                                      int32_t grid_size) const {
    return set_lut3d_ != nullptr ? set_lut3d_(bridge, lut_half4_data, grid_size)
                                 : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::previewBuffer(RtxNgxBridge* bridge, uint32_t slot,
                                           uint64_t* device_ptr, uint32_t* pitch) const {
    return preview_buffer_ != nullptr ? preview_buffer_(bridge, slot, device_ptr, pitch)
                                      : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::downloadPreview(RtxNgxBridge* bridge, void* destination,
                                             size_t destination_bytes) const {
    return download_preview_ != nullptr
               ? download_preview_(bridge, destination, destination_bytes)
               : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::uploadFrame(RtxNgxBridge* bridge, const void* y_plane,
                                         const void* uv_plane, uint32_t y_pitch,
                                         uint32_t uv_pitch, int32_t width, int32_t height,
                                         uint64_t* y_device, uint64_t* uv_device) const {
    return upload_frame_ != nullptr
               ? upload_frame_(bridge, y_plane, uv_plane, y_pitch, uv_pitch, width, height,
                               y_device, uv_device)
               : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::interpolateNv12(RtxNgxBridge* bridge, uint64_t prev_nv12,
                                             uint64_t curr_nv12, uint32_t pitch,
                                             float alpha, uint64_t out_nv12) const {
    return interpolate_nv12_ != nullptr
               ? static_cast<RtxResult>(interpolate_nv12_(bridge, prev_nv12, curr_nv12, pitch, alpha, out_nv12))
               : RTX_ERR_DLL_MISSING;
}

uint64_t NgxRemasterBridge::getInterpBuffer(RtxNgxBridge* bridge, uint32_t width, uint32_t height) const {
    return get_interp_buffer_ != nullptr ? get_interp_buffer_(bridge, width, height) : 0;
}

int32_t NgxRemasterBridge::initRife(RtxNgxBridge* bridge, const char* engine_path, uint32_t width, uint32_t height) const {
    return init_rife_ != nullptr ? init_rife_(bridge, engine_path, width, height) : RTX_ERR_DLL_MISSING;
}

int32_t NgxRemasterBridge::interpolateRife(RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
                                         uint32_t pitch, float alpha, uint64_t out_nv12, int32_t full_range,
                                         int32_t* scene_cut) const {
    return interpolate_rife_ != nullptr
               ? interpolate_rife_(bridge, prev_nv12, curr_nv12, pitch, alpha, out_nv12, full_range, scene_cut)
               : RTX_ERR_DLL_MISSING;
}

int32_t NgxRemasterBridge::buildRifeEngine(const char* onnx_path, const char* engine_out_path,
                                         uint32_t width, uint32_t height, char* err_buf, uint32_t err_cap) const {
    return build_rife_engine_ != nullptr
               ? build_rife_engine_(onnx_path, engine_out_path, width, height, err_buf, err_cap)
               : RTX_ERR_DLL_MISSING;
}

void NgxRemasterBridge::destroyRife(RtxNgxBridge* bridge) const {
    if (destroy_rife_ != nullptr) {
        destroy_rife_(bridge);
    }
}

const char* NgxRemasterBridge::lastError(RtxNgxBridge* bridge) const {
    return last_error_ != nullptr ? last_error_(bridge)
                                  : "rtx_ngx_bridge ABI unavailable";
}

}  // namespace rtx
