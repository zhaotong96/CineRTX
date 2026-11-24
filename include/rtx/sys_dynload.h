// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Wave 12.3 (CINERTX-P12-WORKORDER-005): Zero-named-type dynamic System32 loader
// for optional Windows system DLLs (d3d12, dxgi, ole32, dwmapi, uxtheme, shell32,
// comctl32) plus pure C++ Windows command-line tokenizer (zero shell32 IAT).
//
// Security invariant: every dynamic load strictly passes LOAD_LIBRARY_SEARCH_SYSTEM32
// to LoadLibraryExW, blocking current-working-directory DLL planting attacks.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>

namespace rtx::sys {

// Pure C++ Windows command-line tokenizer matching MSVC CRT argv splitting rules.
// Eliminates static shell32.dll!CommandLineToArgvW import in both headless CLI and GUI.
[[nodiscard]] inline std::vector<std::wstring> parseCommandLineW(const wchar_t* cmd) {
    std::vector<std::wstring> args;
    if (cmd == nullptr) return args;
    const wchar_t* p = cmd;
    while (*p != L'\0') {
        while (*p == L' ' || *p == L'\t') ++p;
        if (*p == L'\0') break;
        std::wstring arg;
        bool in_quotes = false;
        while (*p != L'\0') {
            if (!in_quotes && (*p == L' ' || *p == L'\t')) break;
            if (*p == L'\\') {
                std::size_t bs = 0;
                while (*p == L'\\') { ++bs; ++p; }
                if (*p == L'"') {
                    arg.append(bs / 2, L'\\');
                    if ((bs & 1u) != 0u) {
                        arg.push_back(L'"');
                        ++p;
                    } else {
                        in_quotes = !in_quotes;
                        ++p;
                    }
                } else {
                    arg.append(bs, L'\\');
                }
            } else if (*p == L'"') {
                if (in_quotes && p[1] == L'"') {
                    arg.push_back(L'"');
                    p += 2;
                } else {
                    in_quotes = !in_quotes;
                    ++p;
                }
            } else {
                arg.push_back(*p++);
            }
        }
        args.push_back(std::move(arg));
    }
    return args;
}

[[nodiscard]] inline HMODULE loadSystemDll(const wchar_t* dll_name) noexcept {
    return LoadLibraryExW(dll_name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

// ---- ole32.dll (on-demand COM apartment & CoCreateInstance) ----------------
[[nodiscard]] inline HMODULE ole32Module() noexcept {
    static HMODULE mod = loadSystemDll(L"ole32.dll");
    return mod;
}

[[nodiscard]] inline HRESULT coInitializeEx(void* reserved, DWORD coinit) noexcept {
    using Fn = HRESULT(WINAPI*)(LPVOID, DWORD);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = ole32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CoInitializeEx")) : nullptr;
    }();
    return fn ? fn(reserved, coinit) : E_NOTIMPL;
}

inline void coUninitialize() noexcept {
    using Fn = void(WINAPI*)();
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = ole32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CoUninitialize")) : nullptr;
    }();
    if (fn) fn();
}

inline void coUninitializeGuard(void*) noexcept {
    coUninitialize();
}

[[nodiscard]] inline HRESULT coCreateInstance(REFCLSID rclsid, LPUNKNOWN outer, DWORD ctx,
                                              REFIID riid, LPVOID* ppv) noexcept {
    using Fn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = ole32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CoCreateInstance")) : nullptr;
    }();
    return fn ? fn(rclsid, outer, ctx, riid, ppv) : E_NOTIMPL;
}

inline void coTaskMemFree(LPVOID pv) noexcept {
    if (pv == nullptr) return;
    using Fn = void(WINAPI*)(LPVOID);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = ole32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CoTaskMemFree")) : nullptr;
    }();
    if (fn) fn(pv);
}

inline void coTaskMemFreeVoid(void* pv) noexcept {
    coTaskMemFree(pv);
}

// ---- dxgi.dll (on-demand DXGI factory creation) ----------------------------
[[nodiscard]] inline HMODULE dxgiModule() noexcept {
    static HMODULE mod = loadSystemDll(L"dxgi.dll");
    return mod;
}

[[nodiscard]] inline HRESULT createDxgiFactory1(REFIID riid, void** pp_factory) noexcept {
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = dxgiModule();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CreateDXGIFactory1")) : nullptr;
    }();
    return fn ? fn(riid, pp_factory) : E_NOTIMPL;
}


// ---- d3d12.dll (on-demand D3D12 device & root signature serialization) -----
#if defined(__d3d12_h__)
[[nodiscard]] inline HMODULE d3d12Module() noexcept {
    static HMODULE mod = loadSystemDll(L"d3d12.dll");
    return mod;
}

[[nodiscard]] inline HRESULT d3d12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL min_level,
                                               REFIID riid, void** pp_device) noexcept {
    using Fn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = d3d12Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "D3D12CreateDevice")) : nullptr;
    }();
    return fn ? fn(adapter, min_level, riid, pp_device) : E_NOTIMPL;
}

[[nodiscard]] inline HRESULT d3d12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc,
                                                         D3D_ROOT_SIGNATURE_VERSION version,
                                                         ID3DBlob** blob,
                                                         ID3DBlob** error_blob) noexcept {
    using Fn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                ID3DBlob**, ID3DBlob**);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = d3d12Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "D3D12SerializeRootSignature")) : nullptr;
    }();
    return fn ? fn(desc, version, blob, error_blob) : E_NOTIMPL;
}
#endif

// ---- dwmapi.dll (optional dark title bar) ----------------------------------
inline HRESULT dwmSetWindowAttribute(HWND hwnd, DWORD attr, LPCVOID val, DWORD cb) noexcept {
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = loadSystemDll(L"dwmapi.dll");
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DwmSetWindowAttribute")) : nullptr;
    }();
    return fn ? fn(hwnd, attr, val, cb) : E_NOTIMPL;
}

// ---- uxtheme.dll (optional dark explorer theme) ----------------------------
inline HRESULT setWindowTheme(HWND hwnd, LPCWSTR sub_app, LPCWSTR sub_id) noexcept {
    using Fn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = loadSystemDll(L"uxtheme.dll");
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetWindowTheme")) : nullptr;
    }();
    return fn ? fn(hwnd, sub_app, sub_id) : E_NOTIMPL;
}

// ---- shell32.dll (optional GUI drag-and-drop only, zero static IAT) --------
[[nodiscard]] inline HMODULE shell32Module() noexcept {
    static HMODULE mod = loadSystemDll(L"shell32.dll");
    return mod;
}

inline void dragAcceptFiles(HWND hwnd, BOOL accept) noexcept {
    using Fn = void(WINAPI*)(HWND, BOOL);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = shell32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DragAcceptFiles")) : nullptr;
    }();
    if (fn) fn(hwnd, accept);
}

[[nodiscard]] inline UINT dragQueryFileW(HANDLE hdrop, UINT index, LPWSTR buf, UINT cch) noexcept {
    using Fn = UINT(WINAPI*)(HANDLE, UINT, LPWSTR, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = shell32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DragQueryFileW")) : nullptr;
    }();
    return fn ? fn(hdrop, index, buf, cch) : 0u;
}

inline void dragFinish(HANDLE hdrop) noexcept {
    using Fn = void(WINAPI*)(HANDLE);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = shell32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DragFinish")) : nullptr;
    }();
    if (fn) fn(hdrop);
}

// ---- comctl32.dll (optional GUI common-controls class init, zero static IAT)
inline BOOL initCommonControlsEx(const void* icce) noexcept {
    using Fn = BOOL(WINAPI*)(const void*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = loadSystemDll(L"comctl32.dll");
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "InitCommonControlsEx")) : nullptr;
    }();
    return fn ? fn(icce) : FALSE;
}

}  // namespace rtx::sys

#endif  // _WIN32
