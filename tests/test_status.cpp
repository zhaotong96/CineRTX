// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <string>
#include <utility>

#include "rtx/status.h"

using namespace rtx;

RTX_TEST(status_success_and_failure) {
    const Result<void> success;
    RTX_CHECK(success.has_value());
    RTX_CHECK(static_cast<bool>(success));

    const Result<void> failure = err(ErrorCode::Io, "disk on fire", "sector 7");
    RTX_CHECK(!failure.has_value());
    RTX_CHECK(!static_cast<bool>(failure));
    RTX_CHECK_EQ(failure.error().code, ErrorCode::Io);
    RTX_CHECK_EQ(failure.error().message, std::string("disk on fire"));
    RTX_CHECK_EQ(failure.error().detail, std::string("sector 7"));
    const std::string text = failure.error().toString();
    RTX_CHECK(text.find("IO_ERROR") != std::string::npos);
    RTX_CHECK(text.find("disk on fire") != std::string::npos);
    RTX_CHECK(text.find("sector 7") != std::string::npos);
}

RTX_TEST(status_error_names_are_stable) {
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::Ok)), std::string("OK"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::InvalidArgument)),
                 std::string("INVALID_ARGUMENT"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::DllMissing)), std::string("DLL_MISSING"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::TensorRtVersion)),
                 std::string("TENSORRT_VERSION"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::Internal)), std::string("INTERNAL_ERROR"));
}

RTX_TEST(status_result_value) {
    const Result<int> good = 41;
    RTX_CHECK(good.has_value());
    RTX_CHECK_EQ(good.value(), 41);

    const Result<int> bad = err(ErrorCode::Timeout, "too slow", "2 s");
    RTX_CHECK(!bad.has_value());
    RTX_CHECK_EQ(bad.error().code, ErrorCode::Timeout);
    RTX_CHECK_EQ(bad.error().message, std::string("too slow"));
    RTX_CHECK_EQ(bad.value_or(7), 7);
}

RTX_TEST(status_result_void) {
    Result<void> success;
    RTX_CHECK(success.has_value());

    Result<void> failure = err(ErrorCode::State, "not ready");
    RTX_CHECK(!failure.has_value());
    RTX_CHECK_EQ(failure.error().code, ErrorCode::State);

    failure = err(ErrorCode::OutOfMemory, "no vram");
    RTX_CHECK_EQ(failure.error().code, ErrorCode::OutOfMemory);
}

RTX_TEST(status_error_copy_and_move) {
    Error assigned{ErrorCode::Internal, "stale message", "stale detail"};
    const Error source{ErrorCode::Io, "assigned message", "assigned detail"};
    assigned = source;
    RTX_CHECK_EQ(assigned.message, std::string("assigned message"));
    RTX_CHECK_EQ(assigned.detail, std::string("assigned detail"));

    Error moved = std::move(assigned);
    RTX_CHECK_EQ(moved.message, std::string("assigned message"));
    RTX_CHECK_EQ(moved.detail, std::string("assigned detail"));

    Error move_assigned{ErrorCode::Internal, "old message", "old detail"};
    move_assigned = std::move(moved);
    RTX_CHECK_EQ(move_assigned.message, std::string("assigned message"));
    RTX_CHECK_EQ(move_assigned.detail, std::string("assigned detail"));
}
