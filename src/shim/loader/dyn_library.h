/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Secure runtime symbol resolution.  On Windows the loader restricts the
 * search path to System32 and explicitly registered directories; a hostile
 * nvcuda.dll dropped next to the executable must never be picked up.
 */

#ifndef RTX_SHIM_DYN_LIBRARY_H
#define RTX_SHIM_DYN_LIBRARY_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rtx_shim {

class DynLibrary {
public:
    DynLibrary() = default;
    ~DynLibrary();

    DynLibrary(const DynLibrary&) = delete;
    DynLibrary& operator=(const DynLibrary&) = delete;
    DynLibrary(DynLibrary&& other) noexcept;
    DynLibrary& operator=(DynLibrary&& other) noexcept;

    // Searches System32 first.  On Linux the same API maps to dlopen with an
    // explicit soname and RTLD_NOW | RTLD_LOCAL.
    bool openSystemLibrary(const std::string& name, std::string* error = nullptr);
    bool openExplicitPath(const std::string& path, std::string* error = nullptr);
    void close();

    bool isOpen() const noexcept { return handle_ != nullptr; }
    const std::string& path() const noexcept { return path_; }

    void* symbol(const char* name) const noexcept;
    template <typename Fn>
    Fn symbolAs(const char* name) const noexcept {
        return reinterpret_cast<Fn>(symbol(name));
    }

    std::string lastError() const { return last_error_; }
    static std::string platformName();

private:
    void* handle_ = nullptr;
    std::string path_;
    std::string last_error_;
};

struct ComponentStatus {
    std::string name;
    std::string path;
    bool present = false;
    uint32_t version = 0;
    std::string note;
};

class NvidiaRuntime {
public:
    // Loads nvcuda.dll and resolves the probe subset of the CUDA driver API.
    bool loadCuda(std::string* error = nullptr);
    // Loads nvEncodeAPI64.dll / libnvidia-encode.so.
    bool loadNvenc(std::string* error = nullptr);
    // Loads nvcuvid.dll (decode) without creating a parser.
    bool loadNvcuvid(std::string* error = nullptr);
    // Loads NvOFFRUC.dll (optical flow FRUC).
    bool loadNvOfruc(std::string* error = nullptr);
    // Loads nvinfer_10.dll and queries getInferLibVersion.
    bool loadTensorRt(std::string* error = nullptr);
    // Loads nvsdk_ngx_d.dll plus the VSR/TrueHDR feature DLLs.
    bool loadNgx(std::string* error = nullptr);

    const std::vector<ComponentStatus>& components() const noexcept { return components_; }
    const DynLibrary& cuda() const noexcept { return cuda_; }
    const DynLibrary& nvenc() const noexcept { return nvenc_; }
    const DynLibrary& nvcuvid() const noexcept { return nvcuvid_; }
    const DynLibrary& nvofruc() const noexcept { return nvofruc_; }
    const DynLibrary& tensorrt() const noexcept { return tensorrt_; }
    const DynLibrary& ngx() const noexcept { return ngx_; }

    uint32_t tensorRtVersion() const noexcept { return tensorrt_version_; }
    uint32_t nvencVersion() const noexcept { return nvenc_version_; }
    bool hasTensorRt() const noexcept { return tensorrt_.isOpen(); }
    bool hasNvOfruc() const noexcept { return nvofruc_.isOpen(); }
    bool hasNgx() const noexcept { return ngx_.isOpen(); }

private:
    ComponentStatus& addComponent(const std::string& name);

    DynLibrary cuda_;
    DynLibrary nvenc_;
    DynLibrary nvcuvid_;
    DynLibrary nvofruc_;
    DynLibrary tensorrt_;
    DynLibrary ngx_;
    std::vector<ComponentStatus> components_;
    uint32_t tensorrt_version_ = 0;
    uint32_t nvenc_version_ = 0;
};

}  // namespace rtx_shim

#endif /* RTX_SHIM_DYN_LIBRARY_H */
