/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT 10.13.3.9 dynamic loader and safety version gate.
 */

#include "trt_loader.h"

#include <filesystem>
#include <memory>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rtx_ngx::trt_loader {

namespace {

#if defined(_WIN32)
inline constexpr auto freeDll = [](void* h) noexcept {
    if (h != nullptr) ::FreeLibrary(static_cast<HMODULE>(h));
};
using ScopedDll = std::unique_ptr<void, std::remove_cvref_t<decltype(freeDll)>>;
#else
using ScopedDll = std::unique_ptr<void, void (*)(void*)>;
#endif

#if defined(_WIN32)
ScopedDll g_nvinfer{};
ScopedDll g_nvonnxparser{};
std::wstring g_trt_dir;
#else
void* g_nvinfer = nullptr;
void* g_nvonnxparser = nullptr;
#endif
int32_t g_version = 0;
void* (*g_create_runtime)(void*, int32_t) = nullptr;
void* (*g_create_builder)(void*, int32_t) = nullptr;
#if defined(_WIN32)
int32_t (*g_get_version)() = nullptr;
#endif
void* (*g_create_parser)(void*, void*, int32_t) = nullptr;

#if defined(_WIN32)
std::vector<std::wstring> getCandidateDirectories() {
    std::vector<std::wstring> dirs;
    wchar_t buffer[MAX_PATH] = {};
    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&loadRuntime), &hMod);
    GetModuleFileNameW(hMod, buffer, MAX_PATH);
    std::filesystem::path p(buffer);
    if (const std::filesystem::path parent = p.parent_path(); !parent.empty()) {
        dirs.push_back(parent.wstring());
        dirs.push_back((parent / L"plugins" / L"nvidia_rtx").wstring());
        dirs.push_back((parent.parent_path() / L"plugins" / L"nvidia_rtx").wstring());
        dirs.push_back((parent / L".." / L".." / L".." / L".vs" / L"build" / L"external" / L"tensorrt" /
                        L"TensorRT-10.13.3.9" / L"lib").wstring());
        dirs.push_back((parent / L".." / L".." / L".." / L"build" / L"external" / L"tensorrt" /
                        L"TensorRT-10.13.3.9" / L"lib").wstring());
    }
    dirs.push_back(L"build\\external\\tensorrt\\TensorRT-10.13.3.9\\lib");
    return dirs;
}
#endif

}  // namespace

bool isRuntimeLoaded() noexcept { return g_nvinfer != nullptr; }
bool isParserLoaded() noexcept { return g_nvonnxparser != nullptr; }
int32_t getVersion() noexcept { return g_version; }

bool loadRuntime(std::string* error) {
    if (g_nvinfer != nullptr) return true;
#if defined(_WIN32)
    std::wstring foundDir;
    for (const auto& dir : getCandidateDirectories()) {
        if (std::filesystem::exists(std::filesystem::path(dir) / L"nvinfer_10.dll")) {
            foundDir = dir;
            break;
        }
    }
    if (foundDir.empty()) {
        if (error) *error = "nvinfer_10.dll was not found in any candidate directory";
        return false;
    }
    g_trt_dir = foundDir;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    AddDllDirectory(foundDir.c_str());

    const std::filesystem::path fullPath = std::filesystem::path(foundDir) / L"nvinfer_10.dll";
    ScopedDll h{LoadLibraryExW(fullPath.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
    if (!h) {
        if (error) *error = "LoadLibraryExW failed for " + fullPath.string() + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    auto mod = static_cast<HMODULE>(h.get());
    g_create_runtime = reinterpret_cast<decltype(g_create_runtime)>(GetProcAddress(mod, "createInferRuntime_INTERNAL"));
    g_create_builder = reinterpret_cast<decltype(g_create_builder)>(GetProcAddress(mod, "createInferBuilder_INTERNAL"));
    g_get_version = reinterpret_cast<decltype(g_get_version)>(GetProcAddress(mod, "getInferLibVersion"));
    if (!g_create_runtime || !g_get_version) {
        if (error) *error = "nvinfer_10.dll is missing required export symbols";
        return false;
    }
    g_version = g_get_version();
    if (g_version >= 101400) {
        g_create_runtime = nullptr;
        g_create_builder = nullptr;
        g_get_version = nullptr;
        if (error) {
            *error = "TensorRT version " + std::to_string(g_version) +
                     " >= 10.14 contains known GridSample regression; requires 10.13.3.9";
        }
        return false;
    }
    g_nvinfer = std::move(h);
    return true;
#else
    if (error) *error = "TensorRT runtime dynamic loading unsupported on non-Windows";
    return false;
#endif
}

bool loadParser(std::string* error) {
    if (g_nvonnxparser != nullptr) return true;
    if (!loadRuntime(error)) return false;
#if defined(_WIN32)
    std::filesystem::path fullPath = std::filesystem::path(g_trt_dir) / L"nvonnxparser_10.dll";
    if (!std::filesystem::exists(fullPath)) {
        for (const auto& dir : getCandidateDirectories()) {
            if (std::filesystem::path p = std::filesystem::path(dir) / L"nvonnxparser_10.dll";
                std::filesystem::exists(p)) {
                fullPath = std::move(p);
                break;
            }
        }
    }
    ScopedDll h{LoadLibraryExW(fullPath.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
    if (!h) {
        if (error) *error = "LoadLibraryExW failed for nvonnxparser_10.dll (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    g_create_parser = reinterpret_cast<decltype(g_create_parser)>(
        GetProcAddress(static_cast<HMODULE>(h.get()), "createNvOnnxParser_INTERNAL"));
    if (!g_create_parser) {
        if (error) *error = "nvonnxparser_10.dll is missing createNvOnnxParser_INTERNAL";
        return false;
    }
    g_nvonnxparser = std::move(h);
    return true;
#else
    if (error) *error = "nvonnxparser dynamic loading unsupported on non-Windows";
    return false;
#endif
}

void* createInferRuntime(void* logger) noexcept {
    return loadRuntime() ? g_create_runtime(logger, g_version) : nullptr;
}
void* createInferBuilder(void* logger) noexcept {
    return (loadRuntime() && g_create_builder) ? g_create_builder(logger, g_version) : nullptr;
}
void* createNvOnnxParser(void* network, void* logger) noexcept {
    return (loadParser() && g_create_parser) ? g_create_parser(network, logger, g_version) : nullptr;
}

}  // namespace rtx_ngx::trt_loader
