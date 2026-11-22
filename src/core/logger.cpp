// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/logger.h"

#include <atomic>
#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace rtx {
namespace {

std::atomic<LogLevel> g_min_level{LogLevel::Info};
std::atomic<bool> g_console_enabled{true};
std::mutex g_sink_mutex;
LogSinkCallback g_callback;

constexpr std::string_view shortFileName(std::string_view path) noexcept {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
#if defined(_WIN32)
void emitDebugViewUtf8(std::string_view line) noexcept {
    if (line.empty()) {
        return;
    }
    wchar_t wide_stack[1024];
    const int needed = ::MultiByteToWideChar(
        CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0);
    if (needed <= 0) {
        return;
    }
    if (needed + 1 <= static_cast<int>(std::size(wide_stack))) {
        const int written = ::MultiByteToWideChar(
            CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
            wide_stack, static_cast<int>(std::size(wide_stack)) - 1);
        wide_stack[written > 0 ? written : 0] = L'\0';
        ::OutputDebugStringW(wide_stack);
        return;
    }
    try {
        std::wstring wide(static_cast<size_t>(needed), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                              wide.data(), needed);
        ::OutputDebugStringW(wide.c_str());
    } catch (...) {
    }
}
#endif

}  // namespace

void setLogLevel(LogLevel min_level) noexcept {
    g_min_level.store(min_level, std::memory_order_relaxed);
}
LogLevel currentLogLevel() noexcept {
    return g_min_level.load(std::memory_order_relaxed);
}
void setConsoleLogEnabled(bool enabled) noexcept {
    g_console_enabled.store(enabled, std::memory_order_relaxed);
}
bool isConsoleLogEnabled() noexcept {
    return g_console_enabled.load(std::memory_order_relaxed);
}
void setLogCallback(LogSinkCallback callback) {
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    g_callback = std::move(callback);
}
void logWrite(LogLevel level, std::string_view message, std::source_location loc) noexcept {
    const LogLevel threshold = g_min_level.load(std::memory_order_relaxed);
    if (level == LogLevel::Silent) {
        return;
    }
    if (level == LogLevel::Debug && threshold > LogLevel::Debug) {
        return;
    }
    try {
        const std::string_view file = shortFileName(loc.file_name());
#if defined(_WIN32)
        const unsigned long pid = static_cast<unsigned long>(::GetCurrentProcessId());
        const unsigned long tid = static_cast<unsigned long>(::GetCurrentThreadId());
        const std::string dbg_line = std::format(
            "[CineRTX][P:{}/T:{}][{}][{}:{}] {}\n",
            pid, tid, logLevelName(level), file, loc.line(), message);
        emitDebugViewUtf8(dbg_line);
#endif

        LogSinkCallback cb_copy;
        {
            std::lock_guard<std::mutex> lock(g_sink_mutex);
            cb_copy = g_callback;
        }
        if (cb_copy) {
            cb_copy(level, message);
        }
        if (static_cast<uint8_t>(level) >= static_cast<uint8_t>(threshold) &&
            g_console_enabled.load(std::memory_order_relaxed)) {
            const std::string console_line = std::format(
                "[CineRTX][{}][{}:{}] {}\n",
                logLevelName(level), file, loc.line(), message);
            std::fwrite(console_line.data(), 1, console_line.size(), stderr);
            std::fflush(stderr);
        }
    } catch (...) {
    }
}
}  // namespace rtx
// Logging must never throw across ABI or destructors.
