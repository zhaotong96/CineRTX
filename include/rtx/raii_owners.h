// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.2-R（Std-First）：跨域句柄所有权一律收敛为 std::unique_ptr 类型别名。
// 唯一存留的模板是无状态函数指针删除器 FnDeleter<auto Fn>；凡「删除函数
// 指针 + unique_ptr」能表达的 Win32/COM/CUDA/FFmpeg 资源一律不再手写包装类。
//
// Phase 11.3-R：FnDeleter 升级为 C++23 `static operator()` 配合 C++20 `requires`
// 表达式，编译期自动分派单级指针 `Fn(p)` 与 FFmpeg 二级指针 `Fn(&p)`，彻底消除
// 重复的 IndirectDeleter 模板与空对象 `this` 指针寄存器开销。
//
// COM/D3D12 出参约定（全仓统一，两工具链同构）：std::out_ptr 的代理对象缺
// operator*，绝不能喂给 IID_PPV_ARGS；故一律使用两段式原始指针回填——
//     T* raw = nullptr;
//     if (FAILED(api(..., __uuidof(T), &raw))) return false;  // API 回填
//     target.reset(raw);                                       // 收回所有权
// reset() 先释放旧值再收养新值，出参永远指向已清空的槽位。
//
// 铁律：CUDA 锁页内存只能由 cuMemFreeHost 释放；释放函数缺失时绑定 no-op
// 删除器，宁可泄漏也绝不回退 std::free()——混用释放器会破坏驱动页表并毒化
// CUcontext。系统堆内存只走 std::free，两条路径绝不交叉。

#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <unknwn.h>
#include <windows.h>
#endif

namespace rtx {

// FnDeleter 本体即删除器适配器，依施工单豁免于 @rtx-type-justification 门禁。
template <auto Fn>
struct FnDeleter {
    static void operator()(auto* p) noexcept {
        if (!p) return;
        if constexpr (requires { Fn(p); }) {
            Fn(p);
        } else {
            Fn(&p);  // 统一接管 avcodec_free_context / swr_free / av_frame_free / av_packet_free 等二级指针
        }
    }
};

#if defined(_WIN32)

[[nodiscard]] inline void* normalizeWin32Handle(void* handle) noexcept {
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}
using UniqueHandle = std::unique_ptr<void, FnDeleter<&CloseHandle>>;
using UniqueModule = std::unique_ptr<HINSTANCE__, FnDeleter<&FreeLibrary>>;
using UniqueHwnd = std::unique_ptr<HWND__, FnDeleter<&DestroyWindow>>;
using UniqueBrush = std::unique_ptr<HBRUSH__, FnDeleter<&DeleteObject>>;
using UniqueFont = std::unique_ptr<HFONT__, FnDeleter<&DeleteObject>>;
using UniqueBitmap = std::unique_ptr<HBITMAP__, FnDeleter<&DeleteObject>>;

[[nodiscard]] inline UniqueBrush makeSolidBrush(COLORREF color) noexcept {
    return UniqueBrush{CreateSolidBrush(color)};
}

inline void releaseComUnknown(void* p) noexcept {
    if (p) {
        static_cast<IUnknown*>(p)->Release();
    }
}
template <typename T>
using ComPtr = std::unique_ptr<T, FnDeleter<&releaseComUnknown>>;

#endif  // _WIN32

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using BindCtxCallback = std::move_only_function<void()>;
using HostFreeCallback = std::move_only_function<void(void*)>;
#else
using BindCtxCallback = std::function<void()>;
using HostFreeCallback = std::function<void(void*)>;
#endif

using CudaPinnedHostBuffer = std::pair<std::unique_ptr<void, HostFreeCallback>, size_t>;

inline CudaPinnedHostBuffer wrapCudaPinnedHost(void* ptr, size_t bytes,
                                               int (*free_host)(void*),
                                               BindCtxCallback bind_ctx = {}) noexcept {
    if (ptr == nullptr) {
        return {nullptr, 0};
    }
    auto deleter = [free_host, bind = std::move(bind_ctx)](void* p) mutable {
        if (free_host != nullptr) {
            if (bind) {
                bind();
            }
            free_host(p);
        }
    };
    return {std::unique_ptr<void, HostFreeCallback>(ptr, std::move(deleter)), bytes};
}
inline CudaPinnedHostBuffer wrapSystemHeapHost(void* ptr, size_t bytes) noexcept {
    if (ptr == nullptr) {
        return {nullptr, 0};
    }
    return {std::unique_ptr<void, HostFreeCallback>(ptr, [](void* p) { std::free(p); }), bytes};
}
// Phase 12 (Wave 12.2-C): standard unique_ptr + byte-count pair replaces CudaDeviceBuffer class.
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using CudaDeviceDeleter = std::move_only_function<void(void*)>;
#else
using CudaDeviceDeleter = std::function<void(void*)>;
#endif
using CudaDevicePtr = std::unique_ptr<void, CudaDeviceDeleter>;
using CudaDeviceBuffer = std::pair<CudaDevicePtr, std::size_t>;

[[nodiscard]] inline CudaDeviceBuffer wrapCudaDeviceBuffer(
    uint64_t ptr, size_t bytes, int (*free_fn)(unsigned long long),
    BindCtxCallback bind_ctx = {}) noexcept {
    if (ptr == 0 || free_fn == nullptr) {
        return {CudaDevicePtr{nullptr, [](void*) noexcept {}}, 0u};
    }
    return {CudaDevicePtr(
                reinterpret_cast<void*>(static_cast<uintptr_t>(ptr)),
                [free_fn, bind_ctx = std::move(bind_ctx)](void* p) mutable noexcept {
                    if (p != nullptr) {
                        if (bind_ctx) bind_ctx();
                        (void)free_fn(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)));
                    }
                }),
            bytes};
}

}  // namespace rtx
