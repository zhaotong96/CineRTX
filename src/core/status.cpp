// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/status.h"

namespace rtx {

const char* errorCodeName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:
            return "OK";
        case ErrorCode::InvalidArgument:
            return "INVALID_ARGUMENT";
        case ErrorCode::NotFound:
            return "NOT_FOUND";
        case ErrorCode::Io:
            return "IO_ERROR";
        case ErrorCode::Unsupported:
            return "UNSUPPORTED";
        case ErrorCode::DllMissing:
            return "DLL_MISSING";
        case ErrorCode::ArchUnsupported:
            return "ARCH_UNSUPPORTED";
        case ErrorCode::DriverTooOld:
            return "DRIVER_TOO_OLD";
        case ErrorCode::DeviceLost:
            return "DEVICE_LOST";
        case ErrorCode::OutOfMemory:
            return "OUT_OF_MEMORY";
        case ErrorCode::NgxFailure:
            return "NGX_FAILURE";
        case ErrorCode::TensorRtVersion:
            return "TENSORRT_VERSION";
        case ErrorCode::Config:
            return "CONFIG_ERROR";
        case ErrorCode::State:
            return "INVALID_STATE";
        case ErrorCode::Timeout:
            return "TIMEOUT";
        case ErrorCode::Internal:
            return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

std::string Error::toString() const {
    std::string result = errorCodeName(code);
    result += ": ";
    result += message;
    if (!detail.empty()) {
        result += " (";
        result += detail;
        result += ")";
    }
    return result;
}

}  // namespace rtx
