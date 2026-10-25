/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * C++ convenience wrapper around the pure C backend ABI.  This file is part of
 * the isolated shim layer and must never be included by /src/core.
 */

#ifndef RTX_I_ENGINE_BACKEND_H
#define RTX_I_ENGINE_BACKEND_H

#include <cstdint>
#include <memory>
#include <string>

#include "rtx_shim_abi.h"

namespace rtx_shim {

class EngineBackend {
public:
    EngineBackend() = default;
    explicit EngineBackend(RtxBackend* handle) : handle_(handle) {}
    ~EngineBackend() {
        if (handle_ != nullptr) {
            rtx_shim_destroy_backend(handle_);
        }
    }

    EngineBackend(const EngineBackend&) = delete;
    EngineBackend& operator=(const EngineBackend&) = delete;
    EngineBackend(EngineBackend&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    EngineBackend& operator=(EngineBackend&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr) {
                rtx_shim_destroy_backend(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    static RtxResult create(const RtxBackendConfig& config, std::unique_ptr<EngineBackend>& out,
                            std::string* error = nullptr) {
        RtxBackend* raw = nullptr;
        const RtxResult result = rtx_shim_create_backend(&config, &raw);
        if (result == RTX_OK && raw != nullptr) {
            out = std::unique_ptr<EngineBackend>(new EngineBackend(raw));
        } else if (error != nullptr) {
            *error = rtx_shim_result_name(result);
        }
        return result;
    }

    RtxBackend* get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }

    std::string lastError() const {
        const char* message = handle_ != nullptr ? rtx_shim_last_error(handle_) : nullptr;
        return message != nullptr ? std::string(message) : std::string{};
    }

private:
    RtxBackend* handle_ = nullptr;
};

}  // namespace rtx_shim

#endif /* RTX_I_ENGINE_BACKEND_H */
