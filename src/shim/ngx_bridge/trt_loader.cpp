/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT 10.13.3.9 dynamic loader and safety version gate.
 */

#include "trt_loader.h"

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
#endif

namespace rtx_ngx {

namespace {

#if defined(_WIN32)
std::wstring getModuleDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&TrtLoader::instance), &hMod);
    GetModuleFileNameW(hMod, buffer, MAX_PATH);
    std::filesystem::path p(buffer);
    return p.parent_path().wstring();
}
std::vector<std::wstring> getCandidateDirectories() {
    std::vector<std::wstring> dirs;

    const wchar_t* envRoots[] = { L"RTX_TENSORRT_ROOT", L"TENSORRT_DIR", L"TRT_ROOT" };
    for (const wchar_t* envName : envRoots) {
        wchar_t envVal[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableW(envName, envVal, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::filesystem::path root(envVal);
            dirs.push_back((root / L"lib").wstring());
            dirs.push_back((root / L"bin").wstring());
            dirs.push_back(root.wstring());
        }
    }
    const std::wstring modDir = getModuleDirectory();
    if (!modDir.empty()) {
        std::filesystem::path p(modDir);
        dirs.push_back(p.wstring());
        dirs.push_back((p / L"plugins" / L"nvidia_rtx").wstring());
        dirs.push_back((p.parent_path() / L"plugins" / L"nvidia_rtx").wstring());
        dirs.push_back((p / L".." / L".." / L".." / L"build" / L"external" / L"tensorrt" /
                        L"TensorRT-10.13.3.9" / L"lib").wstring());
    }
    dirs.push_back(L"build\\external\\tensorrt\\TensorRT-10.13.3.9\\lib");

    return dirs;
// Relative path fallback for local developer builds
}
#endif

}  // namespace

TrtLoader& TrtLoader::instance() {
    static TrtLoader loader;
    return loader;
}
TrtLoader::~TrtLoader() {
#if defined(_WIN32)
    if (hNvOnnxParser_) {
        FreeLibrary(reinterpret_cast<HMODULE>(hNvOnnxParser_));
        hNvOnnxParser_ = nullptr;
    }
    if (hNvInfer_) {
        FreeLibrary(reinterpret_cast<HMODULE>(hNvInfer_));
        hNvInfer_ = nullptr;
    }
#endif
}
bool TrtLoader::isRuntimeLoaded() const noexcept {
    return hNvInfer_ != nullptr;
}
bool TrtLoader::isParserLoaded() const noexcept {
    return hNvOnnxParser_ != nullptr;
}
int32_t TrtLoader::getVersion() const noexcept {
    return version_;
}
bool TrtLoader::loadRuntime(std::string* error) {
    if (hNvInfer_ != nullptr) return true;

#if defined(_WIN32)
    std::wstring foundDir;
    for (const auto& dir : getCandidateDirectories()) {
        std::filesystem::path dllPath = std::filesystem::path(dir) / L"nvinfer_10.dll";
        if (std::filesystem::exists(dllPath)) {
            foundDir = dir;
            break;
        }
    }
    if (foundDir.empty()) {
        if (error) *error = "nvinfer_10.dll was not found in any candidate directory";
        return false;
    }
    trtDir_ = foundDir;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    AddDllDirectory(foundDir.c_str());

    std::filesystem::path fullPath = std::filesystem::path(foundDir) / L"nvinfer_10.dll";
    HMODULE h = LoadLibraryExW(fullPath.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) {
        if (error) *error = "LoadLibraryExW failed for " + fullPath.string() + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    createInferRuntime_INTERNAL_ = reinterpret_cast<decltype(createInferRuntime_INTERNAL_)>(
        GetProcAddress(h, "createInferRuntime_INTERNAL"));
    createInferBuilder_INTERNAL_ = reinterpret_cast<decltype(createInferBuilder_INTERNAL_)>(
        GetProcAddress(h, "createInferBuilder_INTERNAL"));
    getInferLibVersion_ = reinterpret_cast<decltype(getInferLibVersion_)>(
        GetProcAddress(h, "getInferLibVersion"));

    if (!createInferRuntime_INTERNAL_ || !getInferLibVersion_) {
        FreeLibrary(h);
        if (error) *error = "nvinfer_10.dll is missing required export symbols";
        return false;
    }
    version_ = getInferLibVersion_();
    if (version_ >= 101400) {
        FreeLibrary(h);
        createInferRuntime_INTERNAL_ = nullptr;
        createInferBuilder_INTERNAL_ = nullptr;
        getInferLibVersion_ = nullptr;
        if (error) {
            *error = "TensorRT version " + std::to_string(version_) +
                     " >= 10.14 contains known GridSample regression; requires 10.13.3.9";
        }
        return false;
    }
    hNvInfer_ = reinterpret_cast<void*>(h);
    return true;
// TensorRT 10.14/10.15 regression barrier
#else
    if (error) *error = "TensorRT runtime dynamic loading unsupported on non-Windows";
    return false;
#endif
}
bool TrtLoader::loadParser(std::string* error) {
    if (hNvOnnxParser_ != nullptr) return true;
    if (!loadRuntime(error)) return false;

#if defined(_WIN32)
    std::filesystem::path fullPath = std::filesystem::path(trtDir_) / L"nvonnxparser_10.dll";
    if (!std::filesystem::exists(fullPath)) {
        for (const auto& dir : getCandidateDirectories()) {
            std::filesystem::path p = std::filesystem::path(dir) / L"nvonnxparser_10.dll";
            if (std::filesystem::exists(p)) {
                fullPath = p;
                break;
            }
        }
    }
    HMODULE h = LoadLibraryExW(fullPath.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) {
        if (error) *error = "LoadLibraryExW failed for nvonnxparser_10.dll (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    createNvOnnxParser_INTERNAL_ = reinterpret_cast<decltype(createNvOnnxParser_INTERNAL_)>(
        GetProcAddress(h, "createNvOnnxParser_INTERNAL"));
    if (!createNvOnnxParser_INTERNAL_) {
        FreeLibrary(h);
        if (error) *error = "nvonnxparser_10.dll is missing createNvOnnxParser_INTERNAL";
        return false;
    }
    hNvOnnxParser_ = reinterpret_cast<void*>(h);
    return true;
#else
    if (error) *error = "nvonnxparser dynamic loading unsupported on non-Windows";
    return false;
#endif
}
void* TrtLoader::createInferRuntime(void* logger) {
    if (!loadRuntime()) return nullptr;
    return createInferRuntime_INTERNAL_(logger, version_);
}
void* TrtLoader::createInferBuilder(void* logger) {
    if (!loadRuntime() || !createInferBuilder_INTERNAL_) return nullptr;
    return createInferBuilder_INTERNAL_(logger, version_);
}
void* TrtLoader::createNvOnnxParser(void* network, void* logger) {
    if (!loadParser() || !createNvOnnxParser_INTERNAL_) return nullptr;
    return createNvOnnxParser_INTERNAL_(network, logger, version_);
}
}  // namespace rtx_ngx
