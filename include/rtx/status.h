// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Modern C++23 std::expected error model for CineRTX.
// Control plane: Error with explicit message & detail (zero heap allocation on success path)
// Data plane: FastError (8B trivial, register passing)

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
    TensorRtVersion = 11,
    Config = 13,
    State = 14,
    Timeout = 15,
    Internal = 16,
};

[[nodiscard]] const char* errorCodeName(ErrorCode code) noexcept;

struct FastError {
    ErrorCode code{ErrorCode::Internal};
    std::int32_t vendor_status{0};

    constexpr FastError() noexcept = default;
    constexpr explicit FastError(ErrorCode c, std::int32_t vs = 0) noexcept
        : code(c), vendor_status(vs) {}
    constexpr bool operator==(const FastError&) const noexcept = default;
};
static_assert(sizeof(FastError) == 8);

template <typename T>
using HotResult = std::expected<T, FastError>;

struct Error {
    ErrorCode code{ErrorCode::Internal};
    std::int32_t vendor_status{0};
    std::string message{};
    std::string detail{};
    std::source_location location{std::source_location::current()};

    constexpr Error() noexcept = default;
    constexpr explicit Error(FastError fast) noexcept
        : code(fast.code), vendor_status(fast.vendor_status) {}

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
    return std::unexpected<FastError>(FastError{code, vendor_status});
}

}  // namespace rtx
