// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <string>

#include "rtx/status.h"

using namespace rtx;

RTX_TEST(status_success_and_failure) {
    const Status success;
    RTX_CHECK(success.ok());
    RTX_CHECK(static_cast<bool>(success));
    RTX_CHECK_EQ(success.code(), ErrorCode::Ok);
    RTX_CHECK_EQ(success.toString(), std::string("OK"));
    RTX_CHECK_EQ(success.message(), std::string{});

    const Status failure =
        Status::fail(ErrorCode::Io, "disk on fire", "sector 7");
    RTX_CHECK(!failure.ok());
    RTX_CHECK(!static_cast<bool>(failure));
    RTX_CHECK_EQ(failure.code(), ErrorCode::Io);
    RTX_CHECK_EQ(failure.message(), std::string("disk on fire"));
    const std::string text = failure.toString();
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
    const Result<int> good = Result<int>::ok(41);
    RTX_CHECK(good.ok());
    RTX_CHECK_EQ(good.value(), 41);
    RTX_CHECK_EQ(good.code(), ErrorCode::Ok);

    const Result<int> bad =
        Result<int>::fail(ErrorCode::Timeout, "too slow", "2 s");
    RTX_CHECK(!bad.ok());
    RTX_CHECK_EQ(bad.code(), ErrorCode::Timeout);
    RTX_CHECK_EQ(bad.error().message, std::string("too slow"));
    RTX_CHECK_EQ(bad.valueOr(7), 7);
    RTX_CHECK(bad.status().code() == ErrorCode::Timeout);

    const Result<int> from_status = Result<int>::fromStatus(
        Status::fail(ErrorCode::Config, "bad config"));
    RTX_CHECK(!from_status.ok());
    RTX_CHECK_EQ(from_status.code(), ErrorCode::Config);
}

RTX_TEST(status_result_void) {
    Result<void> success;
    RTX_CHECK(success.ok());
    RTX_CHECK_EQ(success.status().code(), ErrorCode::Ok);

    Result<void> failure = Result<void>::fail(ErrorCode::State, "not ready");
    RTX_CHECK(!failure.ok());
    RTX_CHECK_EQ(failure.code(), ErrorCode::State);

    const Status converted = failure;  // implicit conversion is used by the API
    RTX_CHECK_EQ(converted.code(), ErrorCode::State);

    failure = Result<void>::fromStatus(Status::fail(ErrorCode::OutOfMemory, "no vram"));
    RTX_CHECK_EQ(failure.code(), ErrorCode::OutOfMemory);
}

RTX_TEST(status_result_from_bool) {
    const Result<int> yes = resultFromBool(true, ErrorCode::Internal, "unused");
    RTX_CHECK(yes.ok());
    const Result<int> no = resultFromBool(false, ErrorCode::Unsupported, "nope");
    RTX_CHECK(!no.ok());
    RTX_CHECK_EQ(no.code(), ErrorCode::Unsupported);
}
