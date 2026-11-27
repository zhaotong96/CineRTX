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
// Order-independent templates/opaque ABI: zero dependency on <d3d12.h> include order.
[[nodiscard]] inline HMODULE d3d12Module() noexcept {
    static HMODULE mod = loadSystemDll(L"d3d12.dll");
    return mod;
}

[[nodiscard]] inline HRESULT d3d12CreateDevice(IUnknown* adapter, int min_level,
                                               REFIID riid, void** pp_device) noexcept {
    using Fn = HRESULT(WINAPI*)(IUnknown*, int, REFIID, void**);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = d3d12Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "D3D12CreateDevice")) : nullptr;
    }();
    return fn ? fn(adapter, min_level, riid, pp_device) : E_NOTIMPL;
}

template <typename DescT, typename BlobT>
[[nodiscard]] inline HRESULT d3d12SerializeRootSignature(const DescT* desc,
                                                         int version,
                                                         BlobT** blob,
                                                         BlobT** error_blob) noexcept {
    using Fn = HRESULT(WINAPI*)(const void*, int, void**, void**);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = d3d12Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "D3D12SerializeRootSignature")) : nullptr;
    }();
    return fn ? fn(desc, version, reinterpret_cast<void**>(blob),
                   reinterpret_cast<void**>(error_blob))
              : E_NOTIMPL;
}

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


// ---- d2d1.dll & dwrite.dll (Wave D-1: on-demand Direct2D / DirectWrite, zero static IAT)
[[nodiscard]] inline HMODULE d2d1Module() noexcept {
    static HMODULE mod = loadSystemDll(L"d2d1.dll");
    return mod;
}

[[nodiscard]] inline HRESULT d2d1CreateFactory(int factory_type, REFIID riid,
                                               const void* factory_options,
                                               void** pp_factory) noexcept {
    using Fn = HRESULT(WINAPI*)(int, REFIID, const void*, void**);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = d2d1Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "D2D1CreateFactory")) : nullptr;
    }();
    return fn ? fn(factory_type, riid, factory_options, pp_factory) : E_NOTIMPL;
}

[[nodiscard]] inline HMODULE dwriteModule() noexcept {
    static HMODULE mod = loadSystemDll(L"dwrite.dll");
    return mod;
}

[[nodiscard]] inline HRESULT dwriteCreateFactory(int factory_type, REFIID riid,
                                                 IUnknown** pp_factory) noexcept {
    using Fn = HRESULT(WINAPI*)(int, REFIID, IUnknown**);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = dwriteModule();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DWriteCreateFactory")) : nullptr;
    }();
    return fn ? fn(factory_type, riid, pp_factory) : E_NOTIMPL;
}

// ---- gdi32.dll (Wave D-1: on-demand GUI-only GDI helpers, zero static IAT) --
[[nodiscard]] inline HMODULE gdi32Module() noexcept {
    static HMODULE mod = loadSystemDll(L"gdi32.dll");
    return mod;
}

[[nodiscard]] inline bool ensureGdi32Loaded() noexcept {
    return gdi32Module() != nullptr;
}

inline BOOL bitBlt(HDC dst, int x, int y, int cx, int cy, HDC src, int x1, int y1, DWORD rop) noexcept {
    using Fn = BOOL(WINAPI*)(HDC, int, int, int, int, HDC, int, int, DWORD);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "BitBlt")) : nullptr;
    }();
    return fn ? fn(dst, x, y, cx, cy, src, x1, y1, rop) : FALSE;
}

[[nodiscard]] inline HBITMAP createCompatibleBitmap(HDC hdc, int cx, int cy) noexcept {
    using Fn = HBITMAP(WINAPI*)(HDC, int, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CreateCompatibleBitmap")) : nullptr;
    }();
    return fn ? fn(hdc, cx, cy) : nullptr;
}

[[nodiscard]] inline HDC createCompatibleDC(HDC hdc) noexcept {
    using Fn = HDC(WINAPI*)(HDC);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CreateCompatibleDC")) : nullptr;
    }();
    return fn ? fn(hdc) : nullptr;
}

[[nodiscard]] inline HFONT createFontW(int cHeight, int cWidth, int cEscapement, int cOrientation,
                                       int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut,
                                       DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
                                       DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName) noexcept {
    using Fn = HFONT(WINAPI*)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CreateFontW")) : nullptr;
    }();
    return fn ? fn(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut,
                   iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName)
              : nullptr;
}

[[nodiscard]] inline HBRUSH createSolidBrush(COLORREF color) noexcept {
    using Fn = HBRUSH(WINAPI*)(COLORREF);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CreateSolidBrush")) : nullptr;
    }();
    return fn ? fn(color) : nullptr;
}

inline BOOL deleteDC(HDC hdc) noexcept {
    using Fn = BOOL(WINAPI*)(HDC);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DeleteDC")) : nullptr;
    }();
    return fn ? fn(hdc) : FALSE;
}

inline BOOL deleteObject(HGDIOBJ obj) noexcept {
    if (obj == nullptr) return TRUE;
    using Fn = BOOL(WINAPI*)(HGDIOBJ);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DeleteObject")) : nullptr;
    }();
    return fn ? fn(obj) : FALSE;
}

[[nodiscard]] inline HGDIOBJ getStockObject(int i) noexcept {
    using Fn = HGDIOBJ(WINAPI*)(int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetStockObject")) : nullptr;
    }();
    return fn ? fn(i) : nullptr;
}

inline BOOL polygon(HDC hdc, const POINT* apt, int cpt) noexcept {
    using Fn = BOOL(WINAPI*)(HDC, const POINT*, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "Polygon")) : nullptr;
    }();
    return fn ? fn(hdc, apt, cpt) : FALSE;
}

inline HGDIOBJ selectObject(HDC hdc, HGDIOBJ h) noexcept {
    using Fn = HGDIOBJ(WINAPI*)(HDC, HGDIOBJ);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SelectObject")) : nullptr;
    }();
    return fn ? fn(hdc, h) : nullptr;
}

inline COLORREF setBkColor(HDC hdc, COLORREF color) noexcept {
    using Fn = COLORREF(WINAPI*)(HDC, COLORREF);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetBkColor")) : nullptr;
    }();
    return fn ? fn(hdc, color) : CLR_INVALID;
}

inline int setBkMode(HDC hdc, int mode) noexcept {
    using Fn = int(WINAPI*)(HDC, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetBkMode")) : nullptr;
    }();
    return fn ? fn(hdc, mode) : 0;
}

inline COLORREF setDCBrushColor(HDC hdc, COLORREF color) noexcept {
    using Fn = COLORREF(WINAPI*)(HDC, COLORREF);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetDCBrushColor")) : nullptr;
    }();
    return fn ? fn(hdc, color) : CLR_INVALID;
}

inline int setPolyFillMode(HDC hdc, int mode) noexcept {
    using Fn = int(WINAPI*)(HDC, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetPolyFillMode")) : nullptr;
    }();
    return fn ? fn(hdc, mode) : 0;
}

inline COLORREF setTextColor(HDC hdc, COLORREF color) noexcept {
    using Fn = COLORREF(WINAPI*)(HDC, COLORREF);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetTextColor")) : nullptr;
    }();
    return fn ? fn(hdc, color) : CLR_INVALID;
}

inline BOOL textOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c) noexcept {
    using Fn = BOOL(WINAPI*)(HDC, int, int, LPCWSTR, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = gdi32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "TextOutW")) : nullptr;
    }();
    return fn ? fn(hdc, x, y, lpString, c) : FALSE;
}

// ---- user32.dll (Wave D-3: GUI-only on-demand windowing -> Static IAT == 1) -
[[nodiscard]] inline HMODULE user32Module() noexcept {
    static HMODULE mod = loadSystemDll(L"user32.dll");
    return mod;
}

[[nodiscard]] inline bool ensureUser32Loaded() noexcept {
    HMODULE m = user32Module();
    return m != nullptr &&
           GetProcAddress(m, "CreateWindowExW") != nullptr &&
           GetProcAddress(m, "DefWindowProcW") != nullptr &&
           GetProcAddress(m, "DispatchMessageW") != nullptr &&
           GetProcAddress(m, "GetMessageW") != nullptr &&
           GetProcAddress(m, "RegisterClassExW") != nullptr;
}

inline BOOL adjustWindowRectExForDpi(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi) noexcept {
    using Fn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "AdjustWindowRectExForDpi")) : nullptr;
    }();
    return fn ? fn(lpRect, dwStyle, bMenu, dwExStyle, dpi) : FALSE;
}

inline HDWP beginDeferWindowPos(int nNumWindows) noexcept {
    using Fn = HDWP(WINAPI*)(int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "BeginDeferWindowPos")) : nullptr;
    }();
    return fn ? fn(nNumWindows) : nullptr;
}

inline HDC beginPaint(HWND hwnd, LPPAINTSTRUCT lpPaint) noexcept {
    using Fn = HDC(WINAPI*)(HWND, LPPAINTSTRUCT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "BeginPaint")) : nullptr;
    }();
    return fn ? fn(hwnd, lpPaint) : nullptr;
}

inline HWND createWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
                            int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
                            HINSTANCE hInstance, LPVOID lpParam) noexcept {
    using Fn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "CreateWindowExW")) : nullptr;
    }();
    return fn ? fn(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
                   hWndParent, hMenu, hInstance, lpParam)
              : nullptr;
}

inline LRESULT defWindowProcW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    using Fn = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DefWindowProcW")) : nullptr;
    }();
    return fn ? fn(hwnd, msg, wParam, lParam) : 0;
}

inline HDWP deferWindowPos(HDWP hWinPosInfo, HWND hwnd, HWND hWndInsertAfter,
                           int x, int y, int cx, int cy, UINT uFlags) noexcept {
    using Fn = HDWP(WINAPI*)(HDWP, HWND, HWND, int, int, int, int, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DeferWindowPos")) : nullptr;
    }();
    return fn ? fn(hWinPosInfo, hwnd, hWndInsertAfter, x, y, cx, cy, uFlags) : nullptr;
}

inline BOOL destroyWindow(HWND hwnd) noexcept {
    if (hwnd == nullptr) return TRUE;
    using Fn = BOOL(WINAPI*)(HWND);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DestroyWindow")) : nullptr;
    }();
    return fn ? fn(hwnd) : FALSE;
}

inline LRESULT dispatchMessageW(const MSG* lpMsg) noexcept {
    using Fn = LRESULT(WINAPI*)(const MSG*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DispatchMessageW")) : nullptr;
    }();
    return fn ? fn(lpMsg) : 0;
}

inline int drawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format) noexcept {
    using Fn = int(WINAPI*)(HDC, LPCWSTR, int, LPRECT, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "DrawTextW")) : nullptr;
    }();
    return fn ? fn(hdc, lpchText, cchText, lprc, format) : 0;
}

inline BOOL enableWindow(HWND hwnd, BOOL bEnable) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, BOOL);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "EnableWindow")) : nullptr;
    }();
    return fn ? fn(hwnd, bEnable) : FALSE;
}

inline BOOL endDeferWindowPos(HDWP hWinPosInfo) noexcept {
    using Fn = BOOL(WINAPI*)(HDWP);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "EndDeferWindowPos")) : nullptr;
    }();
    return fn ? fn(hWinPosInfo) : FALSE;
}

inline BOOL endPaint(HWND hwnd, const PAINTSTRUCT* lpPaint) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, const PAINTSTRUCT*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "EndPaint")) : nullptr;
    }();
    return fn ? fn(hwnd, lpPaint) : FALSE;
}

inline int fillRect(HDC hdc, const RECT* lprc, HBRUSH hbr) noexcept {
    using Fn = int(WINAPI*)(HDC, const RECT*, HBRUSH);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "FillRect")) : nullptr;
    }();
    return fn ? fn(hdc, lprc, hbr) : 0;
}

inline int frameRect(HDC hdc, const RECT* lprc, HBRUSH hbr) noexcept {
    using Fn = int(WINAPI*)(HDC, const RECT*, HBRUSH);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "FrameRect")) : nullptr;
    }();
    return fn ? fn(hdc, lprc, hbr) : 0;
}

inline HWND getAncestor(HWND hwnd, UINT gaFlags) noexcept {
    using Fn = HWND(WINAPI*)(HWND, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetAncestor")) : nullptr;
    }();
    return fn ? fn(hwnd, gaFlags) : nullptr;
}

inline BOOL getClientRect(HWND hwnd, LPRECT lpRect) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, LPRECT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetClientRect")) : nullptr;
    }();
    return fn ? fn(hwnd, lpRect) : FALSE;
}

inline HDC getDC(HWND hwnd) noexcept {
    using Fn = HDC(WINAPI*)(HWND);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetDC")) : nullptr;
    }();
    return fn ? fn(hwnd) : nullptr;
}

inline HWND getDlgItem(HWND hDlg, int nIDDlgItem) noexcept {
    using Fn = HWND(WINAPI*)(HWND, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetDlgItem")) : nullptr;
    }();
    return fn ? fn(hDlg, nIDDlgItem) : nullptr;
}

inline UINT getDlgItemTextW(HWND hDlg, int nIDDlgItem, LPWSTR lpString, int cchMax) noexcept {
    using Fn = UINT(WINAPI*)(HWND, int, LPWSTR, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetDlgItemTextW")) : nullptr;
    }();
    return fn ? fn(hDlg, nIDDlgItem, lpString, cchMax) : 0u;
}

inline UINT getDpiForSystem() noexcept {
    using Fn = UINT(WINAPI*)();
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetDpiForSystem")) : nullptr;
    }();
    return fn ? fn() : 96u;
}

inline UINT getDpiForWindow(HWND hwnd) noexcept {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetDpiForWindow")) : nullptr;
    }();
    return fn ? fn(hwnd) : 96u;
}

inline SHORT getKeyState(int nVirtKey) noexcept {
    using Fn = SHORT(WINAPI*)(int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetKeyState")) : nullptr;
    }();
    return fn ? fn(nVirtKey) : 0;
}

inline BOOL getMessageW(LPMSG lpMsg, HWND hwnd, UINT wMsgFilterMin, UINT wMsgFilterMax) noexcept {
    using Fn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetMessageW")) : nullptr;
    }();
    return fn ? fn(lpMsg, hwnd, wMsgFilterMin, wMsgFilterMax) : FALSE;
}

inline BOOL getMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept {
    using Fn = BOOL(WINAPI*)(HMONITOR, LPMONITORINFO);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetMonitorInfoW")) : nullptr;
    }();
    return fn ? fn(hMonitor, lpmi) : FALSE;
}

inline HWND getParent(HWND hwnd) noexcept {
    using Fn = HWND(WINAPI*)(HWND);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetParent")) : nullptr;
    }();
    return fn ? fn(hwnd) : nullptr;
}

inline LONG_PTR getWindowLongPtrW(HWND hwnd, int nIndex) noexcept {
    using Fn = LONG_PTR(WINAPI*)(HWND, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetWindowLongPtrW")) : nullptr;
    }();
    return fn ? fn(hwnd, nIndex) : 0;
}

inline LONG getWindowLongW(HWND hwnd, int nIndex) noexcept {
    using Fn = LONG(WINAPI*)(HWND, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetWindowLongW")) : nullptr;
    }();
    return fn ? fn(hwnd, nIndex) : 0;
}

inline BOOL getWindowPlacement(HWND hwnd, WINDOWPLACEMENT* lpwndpl) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, WINDOWPLACEMENT*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetWindowPlacement")) : nullptr;
    }();
    return fn ? fn(hwnd, lpwndpl) : FALSE;
}

inline int getWindowTextW(HWND hwnd, LPWSTR lpString, int nMaxCount) noexcept {
    using Fn = int(WINAPI*)(HWND, LPWSTR, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetWindowTextW")) : nullptr;
    }();
    return fn ? fn(hwnd, lpString, nMaxCount) : 0;
}

inline BOOL invalidateRect(HWND hwnd, const RECT* lpRect, BOOL bErase) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, const RECT*, BOOL);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "InvalidateRect")) : nullptr;
    }();
    return fn ? fn(hwnd, lpRect, bErase) : FALSE;
}

inline BOOL isWindow(HWND hwnd) noexcept {
    using Fn = BOOL(WINAPI*)(HWND);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "IsWindow")) : nullptr;
    }();
    return fn ? fn(hwnd) : FALSE;
}

inline BOOL killTimer(HWND hwnd, UINT_PTR uIDEvent) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, UINT_PTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "KillTimer")) : nullptr;
    }();
    return fn ? fn(hwnd, uIDEvent) : FALSE;
}

inline HCURSOR loadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName) noexcept {
    using Fn = HCURSOR(WINAPI*)(HINSTANCE, LPCWSTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "LoadCursorW")) : nullptr;
    }();
    return fn ? fn(hInstance, lpCursorName) : nullptr;
}

inline HICON loadIconW(HINSTANCE hInstance, LPCWSTR lpIconName) noexcept {
    using Fn = HICON(WINAPI*)(HINSTANCE, LPCWSTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "LoadIconW")) : nullptr;
    }();
    return fn ? fn(hInstance, lpIconName) : nullptr;
}

inline int messageBoxA(HWND hwnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) noexcept {
    using Fn = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "MessageBoxA")) : nullptr;
    }();
    return fn ? fn(hwnd, lpText, lpCaption, uType) : 0;
}

inline int messageBoxW(HWND hwnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType) noexcept {
    using Fn = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "MessageBoxW")) : nullptr;
    }();
    return fn ? fn(hwnd, lpText, lpCaption, uType) : 0;
}

inline HMONITOR monitorFromWindow(HWND hwnd, DWORD dwFlags) noexcept {
    using Fn = HMONITOR(WINAPI*)(HWND, DWORD);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "MonitorFromWindow")) : nullptr;
    }();
    return fn ? fn(hwnd, dwFlags) : nullptr;
}

inline BOOL moveWindow(HWND hwnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "MoveWindow")) : nullptr;
    }();
    return fn ? fn(hwnd, X, Y, nWidth, nHeight, bRepaint) : FALSE;
}

inline BOOL postMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "PostMessageW")) : nullptr;
    }();
    return fn ? fn(hwnd, msg, wParam, lParam) : FALSE;
}

inline void postQuitMessage(int nExitCode) noexcept {
    using Fn = void(WINAPI*)(int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "PostQuitMessage")) : nullptr;
    }();
    if (fn) fn(nExitCode);
}

inline ATOM registerClassExW(const WNDCLASSEXW* lpwcx) noexcept {
    using Fn = ATOM(WINAPI*)(const WNDCLASSEXW*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "RegisterClassExW")) : nullptr;
    }();
    return fn ? fn(lpwcx) : 0;
}

inline BOOL registerHotKey(HWND hwnd, int id, UINT fsModifiers, UINT vk) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, int, UINT, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "RegisterHotKey")) : nullptr;
    }();
    return fn ? fn(hwnd, id, fsModifiers, vk) : FALSE;
}

inline BOOL releaseCapture() noexcept {
    using Fn = BOOL(WINAPI*)();
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "ReleaseCapture")) : nullptr;
    }();
    return fn ? fn() : FALSE;
}

inline int releaseDC(HWND hwnd, HDC hdc) noexcept {
    using Fn = int(WINAPI*)(HWND, HDC);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "ReleaseDC")) : nullptr;
    }();
    return fn ? fn(hwnd, hdc) : 0;
}

inline LRESULT sendDlgItemMessageW(HWND hDlg, int nIDDlgItem, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    using Fn = LRESULT(WINAPI*)(HWND, int, UINT, WPARAM, LPARAM);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SendDlgItemMessageW")) : nullptr;
    }();
    return fn ? fn(hDlg, nIDDlgItem, msg, wParam, lParam) : 0;
}

inline LRESULT sendMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    using Fn = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SendMessageW")) : nullptr;
    }();
    return fn ? fn(hwnd, msg, wParam, lParam) : 0;
}

inline HWND setCapture(HWND hwnd) noexcept {
    using Fn = HWND(WINAPI*)(HWND);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetCapture")) : nullptr;
    }();
    return fn ? fn(hwnd) : nullptr;
}

inline BOOL setDlgItemTextW(HWND hDlg, int nIDDlgItem, LPCWSTR lpString) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, int, LPCWSTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetDlgItemTextW")) : nullptr;
    }();
    return fn ? fn(hDlg, nIDDlgItem, lpString) : FALSE;
}

inline BOOL setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT value) noexcept {
    using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetProcessDpiAwarenessContext")) : nullptr;
    }();
    return fn ? fn(value) : FALSE;
}

inline UINT_PTR setTimer(HWND hwnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) noexcept {
    using Fn = UINT_PTR(WINAPI*)(HWND, UINT_PTR, UINT, TIMERPROC);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetTimer")) : nullptr;
    }();
    return fn ? fn(hwnd, nIDEvent, uElapse, lpTimerFunc) : 0u;
}

inline LONG_PTR setWindowLongPtrW(HWND hwnd, int nIndex, LONG_PTR dwNewLong) noexcept {
    using Fn = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetWindowLongPtrW")) : nullptr;
    }();
    return fn ? fn(hwnd, nIndex, dwNewLong) : 0;
}

inline LONG setWindowLongW(HWND hwnd, int nIndex, LONG dwNewLong) noexcept {
    using Fn = LONG(WINAPI*)(HWND, int, LONG);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetWindowLongW")) : nullptr;
    }();
    return fn ? fn(hwnd, nIndex, dwNewLong) : 0;
}

inline BOOL setWindowPlacement(HWND hwnd, const WINDOWPLACEMENT* lpwndpl) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, const WINDOWPLACEMENT*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetWindowPlacement")) : nullptr;
    }();
    return fn ? fn(hwnd, lpwndpl) : FALSE;
}

inline BOOL setWindowPos(HWND hwnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetWindowPos")) : nullptr;
    }();
    return fn ? fn(hwnd, hWndInsertAfter, X, Y, cx, cy, uFlags) : FALSE;
}

inline BOOL setWindowTextW(HWND hwnd, LPCWSTR lpString) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, LPCWSTR);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "SetWindowTextW")) : nullptr;
    }();
    return fn ? fn(hwnd, lpString) : FALSE;
}

inline BOOL showWindow(HWND hwnd, int nCmdShow) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "ShowWindow")) : nullptr;
    }();
    return fn ? fn(hwnd, nCmdShow) : FALSE;
}

inline BOOL translateMessage(const MSG* lpMsg) noexcept {
    using Fn = BOOL(WINAPI*)(const MSG*);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "TranslateMessage")) : nullptr;
    }();
    return fn ? fn(lpMsg) : FALSE;
}

inline BOOL unregisterHotKey(HWND hwnd, int id) noexcept {
    using Fn = BOOL(WINAPI*)(HWND, int);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "UnregisterHotKey")) : nullptr;
    }();
    return fn ? fn(hwnd, id) : FALSE;
}

inline BOOL updateWindow(HWND hwnd) noexcept {
    using Fn = BOOL(WINAPI*)(HWND);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "UpdateWindow")) : nullptr;
    }();
    return fn ? fn(hwnd) : FALSE;
}

inline HWND windowFromDC(HDC hdc) noexcept {
    using Fn = HWND(WINAPI*)(HDC);
    static Fn fn = []() noexcept -> Fn {
        HMODULE m = user32Module();
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "WindowFromDC")) : nullptr;
    }();
    return fn ? fn(hdc) : nullptr;
}

}  // namespace rtx::sys

#endif  // _WIN32
