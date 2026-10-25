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
    std::string message;
    const std::string name = platformLibraryName("nvinfer_10.dll", "libnvinfer.so.10");
    if (!openSdkLibrary(tensorrt_, name, false, &message)) {
        component.note = errorOr(
            message, "TensorRT 10.13.3.9 not found (RIFE disabled; set RTX_NVIDIA_DIR)");
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
    } else {
        component.note = resolve_error;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool NvidiaRuntime::loadNgx(std::string* error) {
    ComponentStatus& component = addComponent("RTX Video SDK (NGX)");
#if defined(_WIN32)
    const char* primary = "nvsdk_ngx_d.dll";
    const char* fallback = "nvsdk_ngx.dll";
#else
    const char* primary = "libnvsdk_ngx.so";
    const char* fallback = "libnvsdk_ngx.so";
#endif
    std::string message;
    bool opened = openSdkLibrary(ngx_, primary, false, &message);
    if (!opened && std::string(fallback) != primary) {
        std::string fallback_message;
        opened = openSdkLibrary(ngx_, fallback, false, &fallback_message);
        if (!opened) {
            message += "; " + fallback_message;
        }
    }
    if (!opened) {
        component.note = errorOr(message, "nvsdk_ngx not found");
        if (error != nullptr) {
            *error = component.note;
        }
        return false;
    }
    component.present = true;
    component.path = ngx_.path();

    const std::string directory = pathToUtf8(utf8ToPath(ngx_.path()).parent_path());
#if defined(_WIN32)
    const char* feature_names[] = {"nvngx_vsr.dll", "nvngx_truehdr.dll"};
#else
    const char* feature_names[] = {"libnvngx_vsr.so", "libnvngx_truehdr.so"};
#endif
    for (const char* feature : feature_names) {
        ComponentStatus& feature_component = addComponent(feature);
        const std::filesystem::path candidate = utf8ToPath(directory) / feature;
        feature_component.present = fileExists(pathToUtf8(candidate));
        feature_component.path = pathToUtf8(candidate);
        feature_component.note =
            feature_component.present ? "feature DLL present" : "feature DLL missing";
    }

    ngx::Table table;
    std::string resolve_error;
    if (table.resolve(ngx_, &resolve_error)) {
        component.note = "NGX exports resolved (D3D12 evaluate entry points)";
    } else {
        component.note = resolve_error;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace rtx_shim
