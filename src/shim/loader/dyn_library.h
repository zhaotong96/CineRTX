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

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
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

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: extern "C" 跨模块导出结构/函数指针表：字段序与调用约定即 shim C ABI 契约，std::function/std::tuple 无法表达 C 布局承诺。
// Invariant: 字段序、对齐与函数指针签名与 ABI 头逐字对应；append-only，禁止重排字段。
struct ComponentStatus {
    std::string name;
    std::string path;
    bool present = false;
    uint32_t version = 0;
    std::string note;
};

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: extern "C" 跨模块导出结构/函数指针表：字段序与调用约定即 shim C ABI 契约，std::function/std::tuple 无法表达 C 布局承诺。
// Invariant: 字段序、对齐与函数指针签名与 ABI 头逐字对应；append-only，禁止重排字段。
class NvidiaRuntime {
public:
    bool loadCuda(std::string* error = nullptr);
// Loads nvcuda.dll and resolves the probe subset of the CUDA driver API.
    bool loadNvenc(std::string* error = nullptr);
// Loads nvEncodeAPI64.dll / libnvidia-encode.so.
    bool loadNvcuvid(std::string* error = nullptr);
// Loads nvcuvid.dll (decode) without creating a parser.
    bool loadNgx(std::string* error = nullptr);
// Loads nvsdk_ngx_d.dll plus the VSR/TrueHDR feature DLLs.

    const std::vector<ComponentStatus>& components() const noexcept { return components_; }
    const DynLibrary& cuda() const noexcept { return cuda_; }
    const DynLibrary& nvenc() const noexcept { return nvenc_; }
    const DynLibrary& nvcuvid() const noexcept { return nvcuvid_; }
    const DynLibrary& ngx() const noexcept { return ngx_; }

    uint32_t nvencVersion() const noexcept { return nvenc_version_; }
    bool hasNgx() const noexcept { return ngx_.isOpen(); }

private:
    ComponentStatus& addComponent(const std::string& name);

    DynLibrary cuda_;
    DynLibrary nvenc_;
    DynLibrary nvcuvid_;
    DynLibrary ngx_;
    std::vector<ComponentStatus> components_;
    uint32_t nvenc_version_ = 0;
};

}  // namespace rtx_shim

#endif /* RTX_SHIM_DYN_LIBRARY_H */
