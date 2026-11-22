// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Zero-dependency C++26 runtime telemetry and diagnostic logger.
//
// Design principles (KISS & Occam's Razor):
//   - Zero external dependencies (no spdlog, no fmtlib, no heap-heavy sinks).
//   - Native Win32 kernel debugger / Sysinternals DebugView & Remote DebugView
//     (OutputDebugStringW) output with unified "[CineRTX]" filter prefix, PID,
//     TID, severity, and source location.
//   - Optional stderr console output (automatically suppressed in pure JSON /
//     --session-stdio mode so stdout/stderr pipes remain machine-clean).
//   - Optional callback hook so Win32 GUI and WebUI (--session-stdio) receive
//     structured real-time log lines without regex scraping.

#pragma once

#include <cstdint>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>

namespace rtx {

enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Silent = 4,
};

[[nodiscard]] constexpr const char* logLevelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Silent:
            return "SILENT";
    }
    return "INFO";
}
using LogSinkCallback = std::function<void(LogLevel level, std::string_view message)>;

void setLogLevel(LogLevel min_level) noexcept;
// Controls minimum severity for stderr console emission (DebugView always
// receives Info/Warn/Error, and Debug when min_level <= LogLevel::Debug).
[[nodiscard]] LogLevel currentLogLevel() noexcept;

void setConsoleLogEnabled(bool enabled) noexcept;
// Enables or disables stderr console emission (e.g. disabled during --json or
// --session-stdio when logs are routed as JSONL events on stdout).
[[nodiscard]] bool isConsoleLogEnabled() noexcept;

void setLogCallback(LogSinkCallback callback);
// Registers an optional in-process listener (GUI status/telemetry or
// --session-stdio JSONL emitter). Pass nullptr / empty function to clear.

void logWrite(LogLevel level, std::string_view message,
// Core non-template sink entry point.
              std::source_location loc = std::source_location::current()) noexcept;

inline void logDebug(std::string_view message,
                     std::source_location loc = std::source_location::current()) noexcept {
    logWrite(LogLevel::Debug, message, loc);
}
inline void logInfo(std::string_view message,
                    std::source_location loc = std::source_location::current()) noexcept {
    logWrite(LogLevel::Info, message, loc);
}
inline void logWarn(std::string_view message,
                    std::source_location loc = std::source_location::current()) noexcept {
    logWrite(LogLevel::Warn, message, loc);
}
inline void logError(std::string_view message,
                     std::source_location loc = std::source_location::current()) noexcept {
    logWrite(LogLevel::Error, message, loc);
}
}  // namespace rtx
