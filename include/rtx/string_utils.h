// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
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

namespace rtx::str {

[[nodiscard]] constexpr std::string_view trimAscii(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}
[[nodiscard]] inline std::string toLowerAscii(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c);
    }
    return out;
}
[[nodiscard]] inline std::string escapeJson(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}
template <typename IntT = int>
[[nodiscard]] constexpr std::optional<IntT> parseInt(std::string_view sv) noexcept {
    sv = trimAscii(sv);
    if (sv.empty()) return std::nullopt;
    IntT val{};
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec != std::errc{} || ptr != sv.data() + sv.size()) return std::nullopt;
    return val;
}
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
[[nodiscard]] inline std::filesystem::path utf8ToPath(std::string_view s) {
    const std::u8string_view u8(reinterpret_cast<const char8_t*>(s.data()), s.size());
    return std::filesystem::path(u8);
}
#if defined(_WIN32)
[[nodiscard]] inline std::wstring utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}
[[nodiscard]] inline std::string wideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}
#endif

} // namespace rtx::str
