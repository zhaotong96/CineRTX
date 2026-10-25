// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Implementation of the rtx_ngx_bridge.dll runtime loader (see
// include/rtx/ngx_remaster.h).  Mirrors nvidia_shim.cpp: candidate paths,
// LoadLibraryExW with restricted search, symbol resolution and an explicit
// ABI version gate before the remaster contract may be used.

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

std::vector<std::string> candidatePaths() {
    std::vector<std::string> paths;
    const char* configured = std::getenv("RTX_NGX_BRIDGE_PATH");
    if (configured != nullptr && configured[0] != '\0') {
        paths.emplace_back(configured);
    }
    const std::string directory = executableDirectory();
    if (!directory.empty()) {
        paths.push_back((std::filesystem::path(directory) / bridgeFileName()).string());
    }
    return paths;
}

}  // namespace

NgxRemasterBridge::~NgxRemasterBridge() { close(); }

template <typename Fn>
bool NgxRemasterBridge::resolve(Fn& function, const char* name, std::string* error) {
#if defined(_WIN32)
    function = reinterpret_cast<Fn>(GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
#else
    function = reinterpret_cast<Fn>(dlsym(handle_, name));
#endif
    if (function == nullptr) {
        if (error != nullptr) {
            *error = std::string("rtx_ngx_bridge is missing the symbol ") + name;
        }
        return false;
    }
    return true;
}

bool NgxRemasterBridge::open(std::string* error) {
    if (loaded()) {
        return true;
    }
    close();
    const std::vector<std::string> candidates = candidatePaths();
    if (candidates.empty()) {
        error_ = "cannot determine the executable directory; set RTX_NGX_BRIDGE_PATH";
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
            last_error = "invalid UTF-8 bridge path: " + candidate;
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
        error_ = last_error.empty() ? "rtx_ngx_bridge not found" : last_error;
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }

    std::string message;
    uint32_t(RTX_SHIM_CALL * abi_version)() = nullptr;
    if (!resolve(abi_version, "rtx_ngx_bridge_abi_version", &message)) {
        // A bridge without the version export predates the remaster contract;
        // treat it as ABI 1 and refuse rather than guess.
        close();
        error_ = "rtx_ngx_bridge does not export rtx_ngx_bridge_abi_version; it predates "
                 "the remaster contract (rebuild the bridge)";
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }
    if (abi_version() < RTX_NGX_BRIDGE_ABI_VERSION) {
        const uint32_t actual = abi_version();
        close();
        error_ = "rtx_ngx_bridge ABI version " + std::to_string(actual) +
                 " is older than the remaster contract " +
                 std::to_string(RTX_NGX_BRIDGE_ABI_VERSION);
        if (error != nullptr) {
            *error = error_;
        }
        return false;
    }

    const bool complete =
        resolve(query_caps_, "rtx_ngx_bridge_query_caps", &message) &&
        resolve(create_, "rtx_ngx_bridge_create", &message) &&
        resolve(destroy_, "rtx_ngx_bridge_destroy", &message) &&
        resolve(remaster_, "rtx_ngx_bridge_remaster", &message) &&
        resolve(get_remaster_stats_, "rtx_ngx_bridge_get_remaster_stats", &message) &&
        resolve(last_error_, "rtx_ngx_bridge_last_error", &message);
    if (!complete) {
        close();
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

void NgxRemasterBridge::close() {
    if (handle_ == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
    path_.clear();
    query_caps_ = nullptr;
    create_ = nullptr;
    destroy_ = nullptr;
    remaster_ = nullptr;
    get_remaster_stats_ = nullptr;
    last_error_ = nullptr;
}

RtxResult NgxRemasterBridge::queryCaps(RtxNgxBridgeCaps* caps) const {
    return query_caps_ != nullptr ? query_caps_(caps) : RTX_ERR_DLL_MISSING;
}

RtxResult NgxRemasterBridge::create(const RtxNgxBridgeConfig* config,
                                    RtxNgxBridge** bridge) const {
    return create_ != nullptr ? create_(config, bridge) : RTX_ERR_DLL_MISSING;
}

void NgxRemasterBridge::destroy(RtxNgxBridge* bridge) const {
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

const char* NgxRemasterBridge::lastError(RtxNgxBridge* bridge) const {
    return last_error_ != nullptr ? last_error_(bridge)
                                  : "rtx_ngx_bridge ABI unavailable";
}

}  // namespace rtx
