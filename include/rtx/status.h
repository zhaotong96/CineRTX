// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Uniform error plumbing for the core engine.  The engine never throws across
// a stage boundary: every fallible operation returns Status or Result<T>.

#pragma once

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

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
    // Slot 12 is permanently retired: it named a runtime "FFmpeg was not
    // compiled in" state.  The media container layer is now an unconditional
    // statically linked core component (Gate 3.0,
    // SPEC-FFMPEG-STATIC-CONSOLIDATION-20261026), so that state cannot exist.
    // The numeric slot stays reserved to keep the remaining values stable for
    // log and trace consumers.
    Config = 13,
    State = 14,
    Timeout = 15,
    Internal = 16,
};

const char* errorCodeName(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
    std::string detail;

    std::string toString() const;
};

class Status {
public:
    Status() = default;
    explicit Status(Error error) : error_(std::move(error)) {}

    static Status success() { return Status{}; }
    static Status fail(ErrorCode code, std::string message, std::string detail = {}) {
        return Status(Error{code, std::move(message), std::move(detail)});
    }

    bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    ErrorCode code() const noexcept { return error_ ? error_->code : ErrorCode::Ok; }
    const Error& error() const { return *error_; }
    const std::string& message() const { return error_ ? error_->message : empty_; }
    std::string toString() const { return error_ ? error_->toString() : std::string("OK"); }

private:
    std::optional<Error> error_;
    inline static const std::string empty_{};
};

template <typename T>
class Result {
public:
    Result() = delete;

    static Result ok(T value) { return Result(std::move(value), std::nullopt); }
    static Result fail(Error error) { return Result(std::nullopt, std::move(error)); }
    static Result fail(ErrorCode code, std::string message, std::string detail = {}) {
        return fail(Error{code, std::move(message), std::move(detail)});
    }
    static Result fromStatus(const Status& status) {
        return fail(status.error());
    }

    bool ok() const noexcept { return value_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & { return *value_; }
    const T& value() const& { return *value_; }
    T&& value() && { return std::move(*value_); }

    T valueOr(T fallback) const { return value_ ? *value_ : std::move(fallback); }
    const Error& error() const { return *error_; }
    ErrorCode code() const noexcept { return error_ ? error_->code : ErrorCode::Ok; }
    Status status() const { return error_ ? Status(*error_) : Status::success(); }

private:
    Result(std::optional<T> value, std::optional<Error> error)
        : value_(std::move(value)), error_(std::move(error)) {}

    std::optional<T> value_;
    std::optional<Error> error_;
};

// Void specialisation.  A default-constructed Result<void> is success; this
// lets stage lambdas simply `return {};`.
template <>
class Result<void> {
public:
    Result() = default;
    explicit Result(Error error) : error_(std::move(error)) {}
    explicit Result(const Status& status) {
        if (!status.ok()) {
            error_ = status.error();
        }
    }

    static Result fail(ErrorCode code, std::string message, std::string detail = {}) {
        return Result(Error{code, std::move(message), std::move(detail)});
    }
    static Result fail(Error error) { return Result(std::move(error)); }
    static Result fromStatus(const Status& status) { return Result(status); }

    bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    operator Status() const { return status(); }
    const Error& error() const { return *error_; }
    ErrorCode code() const noexcept { return error_ ? error_->code : ErrorCode::Ok; }
    Status status() const { return error_ ? Status(*error_) : Status::success(); }

private:
    std::optional<Error> error_;
};

inline Result<int> resultFromBool(bool value, ErrorCode code, std::string message) {
    if (value) {
        return Result<int>::ok(0);
    }
    return Result<int>::fail(code, std::move(message));
}

}  // namespace rtx
