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
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace rtx_shim {

// Phase 12 (Wave 12.2-D): standard tuple + std::shared_ptr<void> custom deleter replaces DynLibrary wrapper.
// Layout: (handle_owner, resolved_path, last_error).
using DynLibrary = std::tuple<std::shared_ptr<void>, std::string, std::string>;

void dynUnloadHandle(void* handle) noexcept;
std::string dynPlatformName();
bool dynOpenSystem(DynLibrary& lib, const std::string& name, std::string* error = nullptr);
bool dynOpenExplicit(DynLibrary& lib, const std::string& path, std::string* error = nullptr);
void dynClose(DynLibrary& lib);
void* dynSymbol(const DynLibrary& lib, const char* name) noexcept;

inline bool dynIsOpen(const DynLibrary& lib) noexcept {
    const auto& [h, p, e] = lib;
    (void)p; (void)e;
    return h != nullptr;
}
inline const std::string& dynPath(const DynLibrary& lib) noexcept {
    const auto& [h, p, e] = lib;
    (void)h; (void)e;
    return p;
}
inline const std::string& dynLastError(const DynLibrary& lib) noexcept {
    const auto& [h, p, e] = lib;
    (void)h; (void)p;
    return e;
}
template <typename Fn>
inline Fn dynSymbolAs(const DynLibrary& lib, const char* name) noexcept {
    return reinterpret_cast<Fn>(dynSymbol(lib, name));
}

// Phase 12 (Wave 12.2-D): standard tuple alias replaces NvidiaRuntime wrapper.
// Layout: (cuda_lib, nvenc_lib, nvcuvid_lib, ngx_lib, components, nvenc_version).
using RuntimeComponent = std::tuple<std::string, std::string, bool, uint32_t, std::string>;
using NvidiaRuntime = std::tuple<DynLibrary, DynLibrary, DynLibrary, DynLibrary,
                                 std::vector<RuntimeComponent>, uint32_t>;

bool rtLoadCuda(NvidiaRuntime& rt, std::string* error = nullptr);
bool rtLoadNvenc(NvidiaRuntime& rt, std::string* error = nullptr);
bool rtLoadNvcuvid(NvidiaRuntime& rt, std::string* error = nullptr);
bool rtLoadNgx(NvidiaRuntime& rt, std::string* error = nullptr);

inline const std::vector<RuntimeComponent>& rtComponents(const NvidiaRuntime& rt) noexcept {
    const auto& [cuda_l, nvenc_l, cuvid_l, ngx_l, comps, ver] = rt;
    (void)cuda_l; (void)nvenc_l; (void)cuvid_l; (void)ngx_l; (void)ver;
    return comps;
}
inline const DynLibrary& rtCuda(const NvidiaRuntime& rt) noexcept {
    const auto& [cuda_l, nvenc_l, cuvid_l, ngx_l, comps, ver] = rt;
    (void)nvenc_l; (void)cuvid_l; (void)ngx_l; (void)comps; (void)ver;
    return cuda_l;
}
inline uint32_t rtNvencVersion(const NvidiaRuntime& rt) noexcept {
    const auto& [cuda_l, nvenc_l, cuvid_l, ngx_l, comps, ver] = rt;
    (void)cuda_l; (void)nvenc_l; (void)cuvid_l; (void)ngx_l; (void)comps;
    return ver;
}

}  // namespace rtx_shim

#endif /* RTX_SHIM_DYN_LIBRARY_H */
