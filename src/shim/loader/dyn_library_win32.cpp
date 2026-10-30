/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Windows loader with a deliberately narrow search order.  A bare library name
 * is resolved from %SystemRoot%\System32 only; SDK DLLs that ship beside the
 * application must be opened by explicit path.  This blocks the classic
 * "rogue nvcuda.dll in the working directory" DLL-planting attack.
 */

#include "dyn_library.h"

#if defined(_WIN32)

#include <windows.h>

#include <algorithm>

namespace rtx_shim {
namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), wide.data(), required);
    return wide;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0, nullptr,
                                             nullptr);
    if (required <= 0) {
        return {};
    }
    std::string output(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(),
                        required, nullptr, nullptr);
    return output;
}

std::string windowsError(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message;
    if (length > 0 && buffer != nullptr) {
        message = narrow(std::wstring(buffer, length));
        while (!message.empty() &&
               (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

bool containsPathSeparator(const std::string& name) {
    return name.find('\\') != std::string::npos || name.find('/') != std::string::npos;
}

}  // namespace

std::string DynLibrary::platformName() { return "windows"; }

bool DynLibrary::openSystemLibrary(const std::string& name, std::string* error) {
    close();
    if (name.empty()) {
        last_error_ = "empty library name";
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    if (containsPathSeparator(name)) {
        return openExplicitPath(name, error);
    }

    wchar_t system_directory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        last_error_ = "GetSystemDirectoryW failed: " + windowsError(GetLastError());
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    std::wstring full_path(system_directory, length);
    if (full_path.back() != L'\\') {
        full_path.push_back(L'\\');
    }
    full_path += widen(name);

    HMODULE module = LoadLibraryExW(full_path.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (module == nullptr) {
        const DWORD first_error = GetLastError();
        // Older Windows versions reject the search flags; retry with the plain
        // loader only for the fully qualified System32 path (still safe).
        module = LoadLibraryExW(full_path.c_str(), nullptr, 0);
        if (module == nullptr) {
            last_error_ = "LoadLibraryExW(" + narrow(full_path) + ") failed: " +
                          windowsError(first_error);
            if (error != nullptr) {
                *error = last_error_;
            }
            return false;
        }
    }
    handle_ = reinterpret_cast<void*>(module);
    path_ = narrow(full_path);
    last_error_.clear();
    return true;
}

bool DynLibrary::openExplicitPath(const std::string& path, std::string* error) {
    close();
    if (path.empty()) {
        last_error_ = "empty library path";
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    const std::wstring wide = widen(path);
    if (wide.empty()) {
        last_error_ = "library path is not valid UTF-8";
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    HMODULE module = LoadLibraryExW(
        wide.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        const DWORD first_error = GetLastError();
        last_error_ = "LoadLibraryExW(" + path + ") failed: " + windowsError(first_error);
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    handle_ = reinterpret_cast<void*>(module);
    path_ = path;
    last_error_.clear();
    return true;
}

void DynLibrary::close() {
    if (handle_ != nullptr) {
        FreeLibrary(reinterpret_cast<HMODULE>(handle_));
        handle_ = nullptr;
    }
    path_.clear();
    last_error_.clear();
}

void* DynLibrary::symbol(const char* name) const noexcept {
    if (handle_ == nullptr || name == nullptr) {
        return nullptr;
    }
    FARPROC address = GetProcAddress(reinterpret_cast<HMODULE>(handle_), name);
    return reinterpret_cast<void*>(address);
}

}  // namespace rtx_shim

#endif  // _WIN32
