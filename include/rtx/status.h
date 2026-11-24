// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Modern C++23 std::expected error model for CineRTX.
// Control plane: Error with explicit message & detail (zero heap allocation on success path)
// Data plane: std::pair<ErrorCode, std::int32_t> (8B trivial, register passing)

#pragma once

#include <cstdint>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#if defined(_MSC_VER)
#define RTX_FORCEINLINE __forceinline
#define RTX_NOINLINE __declspec(noinline)
#else
#define RTX_FORCEINLINE inline
#define RTX_NOINLINE
#endif

namespace rtx {

enum class ErrorCode : int {
    Ok = 0,
    InvalidArgument = 1,
    NotFound = 2,
    Io = 3,
    Unsupported = 4,
    DllMissing = 5,
    ArchUnsupported = 6,
    DriverTooOld = 7,
    DeviceLost = 8,
    OutOfMemory = 9,
    NgxFailure = 10,
    Reserved11 = 11,
    Config = 13,
    State = 14,
    Timeout = 15,
    Internal = 16,
};

[[nodiscard]] const char* errorCodeName(ErrorCode code) noexcept;

template <typename T>
using HotResult = std::expected<T, std::pair<ErrorCode, std::int32_t>>;

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 控制面错误载体需组合 ErrorCode、vendor_status、双层诊断文本及 std::source_location 与 toString() 格式化方法，std::error_code 无法携带动态厂商上下文。
// Invariant: code 为受控枚举值；location 默认锚定出错调用点；toString() 永不返回空串。
struct Error {
    ErrorCode code{ErrorCode::Internal};
    std::int32_t vendor_status{0};
    std::string message{};
    std::string detail{};
    std::source_location location{std::source_location::current()};

    constexpr Error() noexcept = default;
    constexpr explicit Error(std::pair<ErrorCode, std::int32_t> fast) noexcept
        : code(fast.first), vendor_status(fast.second) {}

    Error(ErrorCode c, std::string msg, std::string det = {},
          std::source_location loc = std::source_location::current())
        : code(c), message(std::move(msg)), detail(std::move(det)), location(loc) {}

    [[nodiscard]] std::string toString() const;
};

template <typename T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline auto err(
    ErrorCode code, std::string msg = {}, std::string det = {},
    std::source_location loc = std::source_location::current()) {
    return std::unexpected<Error>(Error{code, std::move(msg), std::move(det), loc});
}
[[nodiscard]] constexpr auto fastErr(ErrorCode code, std::int32_t vendor_status = 0) noexcept {
    return std::unexpected<std::pair<ErrorCode, std::int32_t>>(
        std::pair<ErrorCode, std::int32_t>{code, vendor_status});
}

}  // namespace rtx
