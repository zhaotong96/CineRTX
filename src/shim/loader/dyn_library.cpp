/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 */

#include "dyn_library.h"

namespace rtx_shim {

DynLibrary::~DynLibrary() { close(); }

DynLibrary::DynLibrary(DynLibrary&& other) noexcept
    : handle_(other.handle_), path_(std::move(other.path_)),
      last_error_(std::move(other.last_error_)) {
    other.handle_ = nullptr;
}

DynLibrary& DynLibrary::operator=(DynLibrary&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        path_ = std::move(other.path_);
        last_error_ = std::move(other.last_error_);
        other.handle_ = nullptr;
    }
    return *this;
}

}  // namespace rtx_shim
