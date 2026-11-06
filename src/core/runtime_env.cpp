// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/runtime_env.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <minidumpapiset.h>
#endif

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <locale>

namespace rtx {

void initRuntimeEnvironment() noexcept {
    // 1. C++ global locale configuration with multi-stage fallback.
    // On Linux/glibc, constructing std::locale with a non-existent name throws
    // std::runtime_error. To guarantee noexcept contract is never violated,
    // catch all exceptions and try standard fallback names before falling back to classic().
    try {
        bool cpp_locale_set = false;
        const char* const cpp_candidates[] = {
            ".utf8",
            "C.UTF-8",
            "en_US.UTF-8"
        };
        for (const char* candidate : cpp_candidates) {
            try {
                std::locale::global(std::locale(candidate));
                cpp_locale_set = true;
                break;
            } catch (const std::exception&) {
                // Continue trying candidate locales
            }
        }
        if (!cpp_locale_set) {
            std::locale::global(std::locale::classic());
        }
    } catch (...) {
        // Absolute noexcept guarantee
    }

    // 2. C runtime setlocale with fallback validation.
    try {
        const char* const c_candidates[] = {
            ".utf8",
            "C.UTF-8",
            "en_US.UTF-8",
            "",
            "C"
        };
        for (const char* candidate : c_candidates) {
            if (std::setlocale(LC_ALL, candidate) != nullptr) {
                break;
            }
        }
    } catch (...) {
    }

#if defined(_WIN32)
    // 3. UTF-8 console output and input code pages on Windows
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);

    // 4. Hardened unhandled exception filter for Minidump generation.
    // - Security: load dbghelp.dll strictly from System32 using LOAD_LIBRARY_SEARCH_SYSTEM32
    //   to prevent DLL preloading attacks.
    // - Collision prevention: append PID and timestamp to avoid overwriting existing crash dumps.
    // - Resiliency: prefer Windows temp directory; fallback to current directory if needed.
    ::SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        HMODULE dbghelp = ::LoadLibraryExW(
            L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dbghelp) {
            using MiniDumpWriteDumpFn = BOOL(WINAPI*)(
                HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                PMINIDUMP_EXCEPTION_INFORMATION,
                PMINIDUMP_USER_STREAM_INFORMATION,
                PMINIDUMP_CALLBACK_INFORMATION);
            auto fn = reinterpret_cast<MiniDumpWriteDumpFn>(
                ::GetProcAddress(dbghelp, "MiniDumpWriteDump"));
            if (fn) {
                const DWORD pid = ::GetCurrentProcessId();
                FILETIME ft{};
                ::GetSystemTimeAsFileTime(&ft);
                const uint64_t timestamp =
                    (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

                wchar_t dumpPath[MAX_PATH * 2];
                wchar_t tempDir[MAX_PATH];
                DWORD tempLen = ::GetTempPathW(MAX_PATH, tempDir);
                if (tempLen > 0 && tempLen < MAX_PATH) {
                    swprintf_s(dumpPath, std::size(dumpPath), L"%sCineRTX_crash_%lu_%llu.dmp",
                               tempDir, static_cast<unsigned long>(pid),
                               static_cast<unsigned long long>(timestamp));
                } else {
                    swprintf_s(dumpPath, std::size(dumpPath), L"CineRTX_crash_%lu_%llu.dmp",
                               static_cast<unsigned long>(pid),
                               static_cast<unsigned long long>(timestamp));
                }

                HANDLE hFile = ::CreateFileW(
                    dumpPath, GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    MINIDUMP_EXCEPTION_INFORMATION mei{};
                    mei.ThreadId = ::GetCurrentThreadId();
                    mei.ExceptionPointers = ep;
                    mei.ClientPointers = FALSE;
                    fn(::GetCurrentProcess(), pid,
                       hFile, MiniDumpNormal, &mei, nullptr, nullptr);
                    ::CloseHandle(hFile);
                }
            }
            ::FreeLibrary(dbghelp);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
#endif
}

}  // namespace rtx
