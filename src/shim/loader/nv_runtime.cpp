/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Component discovery for the NVIDIA runtime.  Every library is optional: the
 * engine reports diagnostic status when a component is absent, and the
 * diagnostics list explains exactly what was found.
 */

#include "dyn_library.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "nv_func_tables.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rtx_shim {
namespace {

std::filesystem::path utf8ToPath(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
std::string pathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
#else
    return path.string();
#endif
}
std::string executableDirectory() {
#if defined(_WIN32)
    wchar_t buffer[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length == 0 || length >= 32768) {
        return {};
    }
    return pathToUtf8(std::filesystem::path(std::wstring(buffer, length)).parent_path());
#else
    char buffer[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    return std::filesystem::path(std::string(buffer, static_cast<size_t>(length)))
        .parent_path()
        .string();
#endif
}
bool fileExists(const std::string& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(utf8ToPath(path), error);
}
bool openSdkLibrary(DynLibrary& library, const std::string& name, bool system_fallback,
                    std::string* error) {

    const std::string application_directory = executableDirectory();
// SDK DLLs are allowed to live beside the executable or in a directory the
// operator explicitly registered through RTX_NVIDIA_DIR.  They are never picked
// up from the current working directory or from an unqualified PATH entry.
    if (!application_directory.empty()) {
        const std::filesystem::path candidate =
            utf8ToPath(application_directory) / name;
        if (dynOpenExplicit(library, pathToUtf8(candidate), error)) {
            return true;
        }
    }
    if (system_fallback) {
        return dynOpenSystem(library, name, error);
    }
    if (error != nullptr && error->empty()) {
        *error = "not found in RTX_NVIDIA_DIR, the executable directory or System32";
    }
    return false;
}
std::string platformLibraryName([[maybe_unused]] const char* windows_name, [[maybe_unused]] const char* posix_name) {
#if defined(_WIN32)
    return windows_name;
#else
    return posix_name;
#endif
}
std::string errorOr(const std::string& value, const char* fallback) {
    return value.empty() ? std::string(fallback) : value;
}
}  // namespace

bool openNvidiaLibrary(DynLibrary& library, const char* windows_name,
                       const char* posix_name, bool system_fallback,
                       std::string* error) {
    return openSdkLibrary(library, platformLibraryName(windows_name, posix_name),
                          system_fallback, error);
}
static RuntimeComponent& rtAddComponent(NvidiaRuntime& rt, const std::string& name) {
    auto& [cuda_, nvenc_, nvcuvid_, ngx_, components_, nvenc_version_] = rt;
    (void)cuda_; (void)nvenc_; (void)nvcuvid_; (void)ngx_; (void)nvenc_version_;
    components_.emplace_back(name, std::string{}, false, uint32_t{0}, std::string{});
    return components_.back();
}
bool rtLoadCuda(NvidiaRuntime& rt, std::string* error) {
    auto& [cuda_, nvenc_, nvcuvid_, ngx_, components_, nvenc_version_] = rt;
    (void)nvenc_; (void)nvcuvid_; (void)ngx_; (void)components_; (void)nvenc_version_;
    auto& [comp_name, comp_path, present, version, note] = rtAddComponent(rt, "CUDA driver");
    (void)comp_name; (void)version;
    std::string message;
    if (!dynOpenSystem(cuda_, platformLibraryName("nvcuda.dll", "libcuda.so.1"), &message)) {
        note = errorOr(message, "nvcuda not found");
        if (error != nullptr) *error = note;
        return false;
    }
    present = true;
    comp_path = dynPath(cuda_);
    if (error != nullptr) error->clear();
    return true;
}
bool rtLoadNvenc(NvidiaRuntime& rt, std::string* error) {
    auto& [cuda_, nvenc_, nvcuvid_, ngx_, components_, nvenc_version_] = rt;
    (void)cuda_; (void)nvcuvid_; (void)ngx_; (void)components_;
    auto& [comp_name, comp_path, present, comp_ver, note] = rtAddComponent(rt, "NVENC");
    (void)comp_name;
    std::string message;
    const std::string name = platformLibraryName("nvEncodeAPI64.dll", "libnvidia-encode.so.1");
    if (!openSdkLibrary(nvenc_, name, true, &message)) {
        note = errorOr(message, "NVENC library not found");
        if (error != nullptr) *error = note;
        return false;
    }
    present = true;
    comp_path = dynPath(nvenc_);
    using PFN_NvEncGetMaxSupportedVersion = uint32_t (*)(uint32_t*);
    auto* get_ver = dynSymbolAs<PFN_NvEncGetMaxSupportedVersion>(nvenc_, "NvEncodeAPIGetMaxSupportedVersion");
    if (get_ver == nullptr || dynSymbol(nvenc_, "NvEncodeAPICreateInstance") == nullptr) {
        note = "nvEncodeAPI is missing required exports";
        if (error != nullptr) *error = note;
        dynClose(nvenc_);
        return false;
    }
    uint32_t version = 0;
    if (get_ver(&version) == 0) {
        nvenc_version_ = version;
        comp_ver = version;
    }
    present = true;
    return true;
}
bool rtLoadNvcuvid(NvidiaRuntime& rt, std::string* error) {
    auto& [cuda_, nvenc_, nvcuvid_, ngx_, components_, nvenc_version_] = rt;
    (void)cuda_; (void)nvenc_; (void)ngx_; (void)components_; (void)nvenc_version_;
    auto& [comp_name, comp_path, present, version, note] = rtAddComponent(rt, "NVDEC (cuvid)");
    (void)comp_name; (void)version;
    std::string message;
    const std::string name = platformLibraryName("nvcuvid.dll", "libnvcuvid.so.1");
    if (!openSdkLibrary(nvcuvid_, name, true, &message)) {
        note = errorOr(message, "nvcuvid not found");
        if (error != nullptr) *error = note;
        return false;
    }
    present = true;
    comp_path = dynPath(nvcuvid_);
    if (error != nullptr) error->clear();
    return true;
}
bool rtLoadNgx(NvidiaRuntime& rt, std::string* error) {
    auto& [cuda_, nvenc_, nvcuvid_, ngx_, components_, nvenc_version_] = rt;
    (void)cuda_; (void)nvenc_; (void)nvcuvid_; (void)components_; (void)nvenc_version_;
#if defined(_WIN32)
    const char* primary = "nvsdk_ngx_d.dll";
    const char* fallback = "nvsdk_ngx.dll";
    const char* feature_names[] = {"nvngx_vsr.dll", "nvngx_truehdr.dll"};
#else
    const char* primary = "libnvsdk_ngx.so";
    const char* fallback = "libnvsdk_ngx.so";
    const char* feature_names[] = {"libnvngx_vsr.so", "libnvngx_truehdr.so"};
#endif
    std::vector<std::string> directories;
    const std::string application_directory = executableDirectory();
    if (!application_directory.empty()) {
        const std::filesystem::path root = utf8ToPath(application_directory);
        directories.push_back(pathToUtf8(root / "plugins" / "nvidia_rtx"));
        directories.push_back(application_directory);
    }
    std::string feature_directory;
    bool features_complete = false;
    for (const std::string& directory : directories) {
        bool complete = true;
        for (const char* feature : feature_names) {
            const std::filesystem::path candidate = utf8ToPath(directory) / feature;
            complete = complete && fileExists(pathToUtf8(candidate));
        }
        if (complete) {
            feature_directory = directory;
            features_complete = true;
            break;
        }
    }
    std::string message;
    bool opened = openSdkLibrary(ngx_, primary, false, &message);
    if (!opened && std::string(fallback) != primary) {
        std::string fallback_message;
        opened = openSdkLibrary(ngx_, fallback, false, &fallback_message);
        if (!opened) message += "; " + fallback_message;
    }
    if (!opened && !features_complete) {
        auto& [cn, cp, cpres, cver, cnote] = rtAddComponent(rt, "RTX Video SDK (NGX)");
        (void)cn; (void)cp; (void)cpres; (void)cver;
        cnote = errorOr(message, "nvsdk_ngx not found and the NGX feature DLLs are missing");
        if (error != nullptr) *error = cnote;
        return false;
    }
    if (opened && feature_directory.empty()) {
        feature_directory = pathToUtf8(utf8ToPath(dynPath(ngx_)).parent_path());
    }
    for (const char* feature : feature_names) {
        auto& [fn, fp, fpres, fver, fnote] = rtAddComponent(rt, feature);
        (void)fn; (void)fver;
        const std::filesystem::path candidate =
            feature_directory.empty() ? std::filesystem::path()
                                      : utf8ToPath(feature_directory) / feature;
        fpres = !candidate.empty() && fileExists(pathToUtf8(candidate));
        fp = pathToUtf8(candidate);
        fnote = fpres ? "feature DLL present" : "feature DLL missing";
    }
    auto& [cn, cp, cpres, cver, cnote] = rtAddComponent(rt, "RTX Video SDK (NGX)");
    (void)cn; (void)cver;
    cpres = true;
    cp = opened ? dynPath(ngx_) : feature_directory;
    if (opened) {
        std::string resolve_error;
        if (ngx::resolveExports(ngx_, &resolve_error)) {
            cnote = "NGX exports resolved (D3D12 evaluate entry points)";
        } else {
            cnote = resolve_error;
        }
    } else {
        cnote = "feature DLLs present; the NGX host stub is embedded in rtx_ngx_bridge";
    }
    if (error != nullptr) error->clear();
    return true;
}
}  // namespace rtx_shim
