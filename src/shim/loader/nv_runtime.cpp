/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Component discovery for the NVIDIA runtime.  Every library is optional: the
 * engine degrades to the CPU reference path when a component is absent, and the
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

std::vector<std::string> splitPaths(const std::string& value) {
    std::vector<std::string> paths;
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find(separator, start);
        const std::string part =
            value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            paths.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return paths;
}

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

// SDK DLLs are allowed to live beside the executable or in a directory the
// operator explicitly registered through RTX_NVIDIA_DIR.  They are never picked
// up from the current working directory or from an unqualified PATH entry.
bool openSdkLibrary(DynLibrary& library, const std::string& name, bool system_fallback,
                    std::string* error) {
    const char* configured = std::getenv("RTX_NVIDIA_DIR");
    if (configured != nullptr && configured[0] != '\0') {
        for (const std::string& directory : splitPaths(configured)) {
            const std::filesystem::path candidate = utf8ToPath(directory) / name;
            if (library.openExplicitPath(pathToUtf8(candidate), error)) {
                return true;
            }
        }
    }

    const std::string application_directory = executableDirectory();
    if (!application_directory.empty()) {
        const std::filesystem::path candidate =
            utf8ToPath(application_directory) / name;
        if (library.openExplicitPath(pathToUtf8(candidate), error)) {
            return true;
        }
    }

    if (system_fallback) {
        return library.openSystemLibrary(name, error);
    }
    if (error != nullptr && error->empty()) {
        *error = "not found in RTX_NVIDIA_DIR, the executable directory or System32";
    }
    return false;
}

std::string platformLibraryName(const char* windows_name, [[maybe_unused]] const char* posix_name) {
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

ComponentStatus& NvidiaRuntime::addComponent(const std::string& name) {
    components_.push_back(ComponentStatus{});
    ComponentStatus& component = components_.back();
    component.name = name;
    return component;
}

bool NvidiaRuntime::loadCuda(std::string* error) {
    ComponentStatus& component = addComponent("CUDA driver");
    std::string message;
    if (!cuda_.openSystemLibrary(platformLibraryName("nvcuda.dll", "libcuda.so.1"), &message)) {
        component.note = errorOr(message, "nvcuda not found");
        if (error != nullptr) {
            *error = component.note;
        }
        return false;
    }
    component.present = true;
    component.path = cuda_.path();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool NvidiaRuntime::loadNvenc(std::string* error) {
    ComponentStatus& component = addComponent("NVENC");
    std::string message;
    const std::string name =
        platformLibraryName("nvEncodeAPI64.dll", "libnvidia-encode.so.1");
    if (!openSdkLibrary(nvenc_, name, true, &message)) {
        component.note = errorOr(message, "NVENC library not found");
        if (error != nullptr) {
            *error = component.note;
        }
        return false;
    }
    component.present = true;
    component.path = nvenc_.path();
    nvenc::Table table;
    std::string resolve_error;
    if (table.resolve(nvenc_, &resolve_error)) {
        uint32_t version = 0;
        if (table.getMaxSupportedVersion != nullptr &&
            table.getMaxSupportedVersion(&version) == nvenc::NV_ENC_SUCCESS) {
            nvenc_version_ = version;
            component.version = static_cast<int32_t>(version);
        }
    } else {
        component.note = resolve_error;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool NvidiaRuntime::loadNvcuvid(std::string* error) {
    ComponentStatus& component = addComponent("NVDEC (cuvid)");
    std::string message;
    const std::string name = platformLibraryName("nvcuvid.dll", "libnvcuvid.so.1");
    if (!openSdkLibrary(nvcuvid_, name, true, &message)) {
        component.note = errorOr(message, "nvcuvid not found");
        if (error != nullptr) {
            *error = component.note;
        }
        return false;
    }
    component.present = true;
    component.path = nvcuvid_.path();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool NvidiaRuntime::loadNvOfruc(std::string* error) {
    ComponentStatus& component = addComponent("NvOFFRUC");
    std::string message;
    const std::string name = platformLibraryName("NvOFFRUC.dll", "libNvOFFRUC.so");
    if (!openSdkLibrary(nvofruc_, name, false, &message)) {
        component.note = errorOr(message, "NvOFFRUC not found (optical-flow FRUC disabled)");
        if (error != nullptr) {
            *error = component.note;
        }
        return false;
    }
    component.present = true;
    component.path = nvofruc_.path();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool NvidiaRuntime::loadTensorRt(std::string* error) {
    ComponentStatus& component = addComponent("TensorRT");
    // The pinned runtime is TensorRT 10.13.3.9, whose module is nvinfer_10.dll.
    // The 11.x archive renames it to nvinfer_11.dll, so both are probed: the
    // component must report what is actually installed, and the RIFE version
    // gate (kRifeTensorRtBaseline in include/rtx/rife.h) is what refuses to
    // build an engine on anything but 10.13.3.9.  Probing a library is not a
    // promise that the graph will build on it.
    const struct {
        const char* windows_name;
        const char* posix_name;
    } candidates[] = {
        {"nvinfer_10.dll", "libnvinfer.so.10"},
        {"nvinfer_11.dll", "libnvinfer.so.11"},
    };

    std::string message;
    bool opened = false;
    for (const auto& candidate : candidates) {
        std::string attempt;
        const std::string name =
            platformLibraryName(candidate.windows_name, candidate.posix_name);
        if (openSdkLibrary(tensorrt_, name, false, &attempt)) {
            opened = true;
            break;
        }
        if (!message.empty()) {
            message += "; ";
        }
        message += attempt;
    }
    if (!opened) {
        component.note = errorOr(
            message,
            "TensorRT 10.13.3.9 (nvinfer_10.dll) not found beside CineRTX.exe; "
            "RIFE disabled (set RTX_NVIDIA_DIR)");
        if (error != nullptr) {
            *error = component.note;
        }
        return false;
    }
    component.present = true;
    component.path = tensorrt_.path();
    tensorrt::Table table;
    std::string resolve_error;
    if (table.resolve(tensorrt_, &resolve_error)) {
        const int32_t version = table.getInferLibVersion();
        tensorrt_version_ = static_cast<uint32_t>(version);
        component.version = version;
        component.note = "TensorRT " + tensorrt::versionToString(version);
        // NV_TENSORRT_VERSION packs major*10000 + minor*100 + patch, so the
        // pinned 10.13.3.9 runtime reports 101303.  The build number is not part
        // of the value; the RIFE gate compares the full header version instead.
        constexpr int32_t kRifeRuntimeVersion = 10 * 10000 + 13 * 100 + 3;
        if (version != kRifeRuntimeVersion) {
            component.note += " (RIFE engine build requires 10.13.3.9)";
        }
    } else {
        component.note = resolve_error;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool NvidiaRuntime::loadNgx(std::string* error) {
#if defined(_WIN32)
    const char* primary = "nvsdk_ngx_d.dll";
    const char* fallback = "nvsdk_ngx.dll";
    const char* feature_names[] = {"nvngx_vsr.dll", "nvngx_truehdr.dll"};
#else
    const char* primary = "libnvsdk_ngx.so";
    const char* fallback = "libnvsdk_ngx.so";
    const char* feature_names[] = {"libnvngx_vsr.so", "libnvngx_truehdr.so"};
#endif

    // The proprietary host stub is linked into rtx_ngx_bridge, not into this
    // shim, so nvsdk_ngx_d.dll is not a deliverable.  The feature DLLs are the
    // real signal that the plugin is deployed; look where the bridge looks.
    std::vector<std::string> directories;
    const char* plugin_directory = std::getenv("RTX_NGX_PLUGIN_DIR");
    if (plugin_directory != nullptr && plugin_directory[0] != '\0') {
        directories.emplace_back(plugin_directory);
    }
    if (const char* nvidia_directory = std::getenv("RTX_NVIDIA_DIR");
        nvidia_directory != nullptr && nvidia_directory[0] != '\0') {
        for (const std::string& entry : splitPaths(nvidia_directory)) {
            directories.push_back(entry);
        }
    }
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
        if (!opened) {
            message += "; " + fallback_message;
        }
    }
    if (!opened && !features_complete) {
        ComponentStatus& component = addComponent("RTX Video SDK (NGX)");
        component.note = errorOr(message, "nvsdk_ngx not found and the NGX feature DLLs are missing");
        if (error != nullptr) {
            *error = component.note;
        }
        return false;
    }

    if (opened && feature_directory.empty()) {
        feature_directory = pathToUtf8(utf8ToPath(ngx_.path()).parent_path());
    }

    for (const char* feature : feature_names) {
        ComponentStatus& feature_component = addComponent(feature);
        const std::filesystem::path candidate =
            feature_directory.empty() ? std::filesystem::path()
                                      : utf8ToPath(feature_directory) / feature;
        feature_component.present =
            !candidate.empty() && fileExists(pathToUtf8(candidate));
        feature_component.path = pathToUtf8(candidate);
        feature_component.note =
            feature_component.present ? "feature DLL present" : "feature DLL missing";
    }

    ComponentStatus& component = addComponent("RTX Video SDK (NGX)");
    component.present = true;
    component.path = opened ? ngx_.path() : feature_directory;
    if (opened) {
        ngx::Table table;
        std::string resolve_error;
        if (table.resolve(ngx_, &resolve_error)) {
            component.note = "NGX exports resolved (D3D12 evaluate entry points)";
        } else {
            component.note = resolve_error;
        }
    } else {
        component.note =
            "feature DLLs present; the NGX host stub is embedded in rtx_ngx_bridge";
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace rtx_shim
